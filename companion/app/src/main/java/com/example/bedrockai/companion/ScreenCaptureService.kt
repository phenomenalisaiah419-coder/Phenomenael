package com.example.bedrockai.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.SystemClock
import android.content.pm.ServiceInfo

class ScreenCaptureService : Service() {
    private var projection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var imageReader: ImageReader? = null
    private var frameThread: HandlerThread? = null
    private var overlay: PanicOverlay? = null
    private var lastFrameAtMs = 0L

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            CompanionRuntime.panicStop()
            stopCapture()
            stopSelf(startId)
            return START_NOT_STICKY
        }
        if (intent?.action == ACTION_SHOW_OVERLAY) {
            showOverlayIfPermitted()
            return START_NOT_STICKY
        }
        if (CompanionRuntime.captureActive) return START_NOT_STICKY

        val resultCode = intent?.getIntExtra(EXTRA_RESULT_CODE, Int.MIN_VALUE) ?: Int.MIN_VALUE
        val resultData = readResultData(intent)
        if (resultCode == Int.MIN_VALUE || resultData == null) {
            CompanionRuntime.lastCaptureError = "Screen-capture permission data is missing. Start again from the app."
            stopSelf(startId)
            return START_NOT_STICKY
        }

        try {
            CompanionRuntime.clearFrames()
            ScreenFrameAnalyzer.reset()
            createNotificationChannel()
            val notification = buildNotification()
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
            } else {
                startForeground(NOTIFICATION_ID, notification)
            }
            val manager = getSystemService(MediaProjectionManager::class.java)
                ?: error("MediaProjectionManager is unavailable")
            val mediaProjection = manager.getMediaProjection(resultCode, resultData)
                ?: error("Android did not grant a screen-capture session")
            projection = mediaProjection
            mediaProjection.registerCallback(object : MediaProjection.Callback() {
                override fun onStop() {
                    stopCapture()
                    stopSelf()
                }

                override fun onCapturedContentResize(width: Int, height: Int) {
                    resizeCapture(width, height)
                }
            }, Handler(mainLooper))
            startFrameReader(mediaProjection)
            showOverlayIfPermitted()
            CompanionRuntime.captureActive = true
            CompanionRuntime.lastCaptureError = ""
        } catch (error: Exception) {
            CompanionRuntime.lastCaptureError = error.message ?: error.javaClass.simpleName
            stopCapture()
            stopSelf(startId)
        }
        return START_NOT_STICKY
    }

    private fun showOverlayIfPermitted() {
        if (overlay != null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M ||
            !android.provider.Settings.canDrawOverlays(this) || !CompanionRuntime.captureActive) return
        overlay = PanicOverlay(this) {
            CompanionRuntime.panicStop()
            stopCapture()
            stopSelf()
        }.also { it.show() }
    }

    private fun startFrameReader(mediaProjection: MediaProjection) {
        val display = DisplayGeometry.read(this)
        val width = display.width
        val height = display.height
        val density = display.densityDpi

        val thread = HandlerThread("BedrockCompanionFrames")
        thread.start()
        frameThread = thread
        val reader = ImageReader.newInstance(width, height, android.graphics.PixelFormat.RGBA_8888, 2)
        imageReader = reader
        installFrameListener(reader)

        virtualDisplay = mediaProjection.createVirtualDisplay(
            "BedrockCompanionCapture",
            width,
            height,
            density,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            reader.surface,
            null,
            Handler(thread.looper)
        ) ?: error("Android could not create the capture display")
    }

    private fun resizeCapture(width: Int, height: Int) {
        if (width <= 0 || height <= 0 || frameThread == null) return
        val display = DisplayGeometry.read(this)
        val oldReader = imageReader
        val newReader = ImageReader.newInstance(width, height, android.graphics.PixelFormat.RGBA_8888, 2)
        installFrameListener(newReader)
        try {
            virtualDisplay?.resize(width, height, display.densityDpi)
            virtualDisplay?.setSurface(newReader.surface)
            imageReader = newReader
            oldReader?.setOnImageAvailableListener(null, null)
            oldReader?.close()
        } catch (error: Exception) {
            newReader.close()
            CompanionRuntime.lastCaptureError = "Display resize: ${error.message ?: error.javaClass.simpleName}"
        }
    }

    private fun installFrameListener(reader: ImageReader) {
        val thread = frameThread ?: return
        reader.setOnImageAvailableListener({ source ->
            val now = SystemClock.elapsedRealtime()
            if (now - lastFrameAtMs < FRAME_INTERVAL_MS) {
                val skipped = source.acquireLatestImage()
                skipped?.close()
                return@setOnImageAvailableListener
            }
            lastFrameAtMs = now
            val image = source.acquireLatestImage() ?: return@setOnImageAvailableListener
            try {
                ScreenFrameAnalyzer.analyze(image)?.let(CompanionRuntime::publishFrame)
            } catch (error: Exception) {
                CompanionRuntime.lastCaptureError = "Frame analysis: ${error.message ?: error.javaClass.simpleName}"
            } finally {
                image.close()
            }
        }, Handler(thread.looper))
    }

    private fun readResultData(intent: Intent?): Intent? {
        if (intent == null) return null
        return if (Build.VERSION.SDK_INT >= 33) {
            intent.getParcelableExtra(EXTRA_RESULT_DATA, Intent::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(EXTRA_RESULT_DATA)
        }
    }

    private fun buildNotification(): Notification {
        val openApp = PendingIntent.getActivity(
            this, 1, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val stop = PendingIntent.getService(
            this, 2, Intent(this, ScreenCaptureService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
        }
        return builder
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .setContentTitle("Bedrock Companion is capturing")
            .setContentText("Screen frames stay on this device. Tap Stop to end capture.")
            .setContentIntent(openApp)
            .setOngoing(true)
            .addAction(
                Notification.Action.Builder(
                    android.graphics.drawable.Icon.createWithResource(this, android.R.drawable.ic_media_pause),
                    "STOP",
                    stop
                ).build()
            )
            .build()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val manager = getSystemService(NotificationManager::class.java) ?: return
            manager.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "Bedrock Companion capture", NotificationManager.IMPORTANCE_LOW)
            )
        }
    }

    private fun stopCapture() {
        CompanionRuntime.captureActive = false
        CompanionRuntime.panicStop()
        overlay?.hide()
        overlay = null
        virtualDisplay?.release()
        virtualDisplay = null
        imageReader?.setOnImageAvailableListener(null, null)
        imageReader?.close()
        imageReader = null
        frameThread?.quitSafely()
        frameThread = null
        val oldProjection = projection
        projection = null
        oldProjection?.let { current ->
            try { current.stop() } catch (_: Exception) { }
        }
        ScreenFrameAnalyzer.reset()
        stopForeground(STOP_FOREGROUND_REMOVE)
    }

    override fun onDestroy() {
        stopCapture()
        super.onDestroy()
    }

    companion object {
        const val ACTION_STOP = "com.example.bedrockai.companion.STOP_CAPTURE"
        const val ACTION_SHOW_OVERLAY = "com.example.bedrockai.companion.SHOW_OVERLAY"
        const val EXTRA_RESULT_CODE = "capture_result_code"
        const val EXTRA_RESULT_DATA = "capture_result_data"
        private const val CHANNEL_ID = "bedrock_companion_capture"
        private const val NOTIFICATION_ID = 4107
        private const val FRAME_INTERVAL_MS = 200L

        fun stop(context: Context) {
            context.startService(Intent(context, ScreenCaptureService::class.java).setAction(ACTION_STOP))
        }
    }
}
