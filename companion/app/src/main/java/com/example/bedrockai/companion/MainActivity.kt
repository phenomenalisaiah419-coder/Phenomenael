package com.example.bedrockai.companion

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.graphics.Bitmap
import android.graphics.Color
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import java.util.Locale

class MainActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var accessibilityStatus: TextView
    private lateinit var brainStatus: TextView
    private lateinit var preview: ImageView
    private var previewFrameTime = -1L
    private val handler = Handler(Looper.getMainLooper())
    private var polling = false

    private val refresh = object : Runnable {
        override fun run() {
            refreshStatus()
            if (polling) handler.postDelayed(this, 700L)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        buildUi()
        requestNotificationPermission()
        refreshStatus()
    }

    override fun onResume() {
        super.onResume()
        if (CompanionRuntime.captureActive && Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && Settings.canDrawOverlays(this)) {
            startService(Intent(this, ScreenCaptureService::class.java).setAction(ScreenCaptureService.ACTION_SHOW_OVERLAY))
        }
        polling = true
        handler.post(refresh)
    }

    override fun onPause() {
        polling = false
        handler.removeCallbacks(refresh)
        super.onPause()
    }

    private fun buildUi() {
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(18), dp(18), dp(18), dp(28))
        }
        val title = TextView(this).apply {
            text = "BEDROCK COMPANION · STAGE 1"
            textSize = 19f
            setTextColor(0xff17202a.toInt())
            setPadding(0, 0, 0, dp(8))
        }
        val intro = TextView(this).apply {
            text = "Controls your existing Minecraft touch screen. No second player or Xbox login. Rotate to landscape, grant capture and accessibility permissions, then use the floating controls in Minecraft. Test commands are manual and short; autonomous control stays OFF."
            textSize = 14f
            setTextColor(0xff394655.toInt())
            setPadding(0, 0, 0, dp(14))
        }
        status = infoText()
        accessibilityStatus = infoText()
        brainStatus = infoText()
        preview = ImageView(this).apply {
            adjustViewBounds = true
            scaleType = ImageView.ScaleType.FIT_CENTER
            setBackgroundColor(0xff1b1b1b.toInt())
            contentDescription = "Last in-memory Minecraft screen capture preview"
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(150))
        }
        content.addView(title)
        content.addView(intro)
        content.addView(sectionTitle("1–3 · Capture and connect to Minecraft"))
        content.addView(actionButton("Start screen capture") { requestScreenCapture() })
        content.addView(actionButton("Stop capture / panic stop") { ScreenCaptureService.stop(this) })
        content.addView(actionButton("Open Minecraft") { openMinecraft() })
        content.addView(status)
        content.addView(preview)
        content.addView(TextView(this).apply {
            text = "Preview is a small grayscale frame kept in RAM. Stop capture from the floating panel or notification, then return here to inspect the last captured frame. It is not saved or uploaded."
            textSize = 12f
            setTextColor(0xff5b6570.toInt())
            setPadding(0, dp(4), 0, dp(6))
        })
        content.addView(sectionTitle("4 · Enable touch control"))
        content.addView(actionButton("Enable Bedrock Companion accessibility") {
            startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
        })
        content.addView(accessibilityStatus)
        content.addView(actionButton("Allow floating STOP / test panel") { requestOverlayPermission() })
        content.addView(sectionTitle("Touch calibration · screen percentages"))
        val layout = ControlLayout.load(this)
        val joyX = numericField("Joystick centre X", layout.joystickX)
        val joyY = numericField("Joystick centre Y", layout.joystickY)
        val forwardY = numericField("Forward point Y", layout.forwardY)
        val lookStart = numericField("Look swipe start X", layout.lookStartX)
        val lookEnd = numericField("Look swipe end X", layout.lookEndX)
        val lookY = numericField("Look swipe Y", layout.lookY)
        listOf(joyX, joyY, forwardY, lookStart, lookEnd, lookY).forEach { content.addView(it) }
        content.addView(actionButton("Save touch positions") {
            val newLayout = ControlLayout(
                joystickX = percent(joyX, layout.joystickX),
                joystickY = percent(joyY, layout.joystickY),
                forwardY = percent(forwardY, layout.forwardY),
                lookStartX = percent(lookStart, layout.lookStartX),
                lookEndX = percent(lookEnd, layout.lookEndX),
                lookY = percent(lookY, layout.lookY)
            )
            newLayout.save(this)
            toast("Touch positions saved")
        })
        content.addView(sectionTitle("5 · Manual control checks"))
        content.addView(TextView(this).apply {
            text = "After capture starts, open Minecraft. Use the floating FWD and TURN controls over Minecraft; they only dispatch when Minecraft is the foreground app. STOP ends capture immediately."
            textSize = 13f
            setTextColor(0xff34495e.toInt())
            setPadding(0, 0, 0, dp(8))
        })
        content.addView(sectionTitle("6 · v0.35 native safety check"))
        content.addView(actionButton("Run safe native brain self-check") { runBrainSelfCheck() })
        content.addView(brainStatus)
        content.addView(TextView(this).apply {
            text = "Current boundary: screenshot capture and gesture transport are implemented here. The screen parser does not yet reliably read Minecraft health, position, hotbar, enemies or terrain. The native self-check supplies no game data and confirms that unknown servers remain observe-only; autonomous control is not enabled."
            textSize = 13f
            setTextColor(0xff6b3f12.toInt())
            setPadding(0, dp(12), 0, dp(4))
        })

        val scroll = ScrollView(this).apply {
            addView(content, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
        setContentView(scroll)
    }

    private fun requestScreenCapture() {
        val manager = getSystemService(MediaProjectionManager::class.java)
        if (manager == null) {
            toast("Screen capture is unavailable on this device")
            return
        }
        CompanionRuntime.clearFrames()
        previewFrameTime = -1L
        preview.setImageDrawable(null)
        startActivityForResult(manager.createScreenCaptureIntent(), REQUEST_CAPTURE)
    }

    @Deprecated("Kept compatible with minSdk 26 without an AndroidX Activity dependency")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQUEST_CAPTURE) return
        if (resultCode != RESULT_OK || data == null) {
            toast("Screen capture permission was not granted")
            return
        }
        val service = Intent(this, ScreenCaptureService::class.java).apply {
            putExtra(ScreenCaptureService.EXTRA_RESULT_CODE, resultCode)
            putExtra(ScreenCaptureService.EXTRA_RESULT_DATA, data)
        }
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(service) else startService(service)
            toast("Capture starting. Choose Entire screen if Android offers a choice.")
        } catch (error: Exception) {
            toast("Could not start capture: ${error.message}")
        }
    }

    private fun openMinecraft() {
        if (!CompanionRuntime.captureActive) {
            toast("Start screen capture first")
            return
        }
        if (!MinecraftTouchAccessibilityService.isConnected()) {
            toast("Enable Bedrock Companion accessibility first")
            return
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            toast("Allow Display over other apps so the floating test and STOP controls can appear")
            return
        }
        val launch = packageManager.getLaunchIntentForPackage("com.mojang.minecraftpe")
        if (launch == null) {
            toast("Minecraft was not found under its standard package name")
            return
        }
        launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        startActivity(launch)
    }

    private fun requestOverlayPermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M || Settings.canDrawOverlays(this)) {
            toast("Floating panel permission is already available. Restart capture to show it.")
            return
        }
        val intent = Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName"))
        startActivity(intent)
    }

    private fun refreshStatus() {
        val frame = CompanionRuntime.latestFrame
        status.text = when {
            CompanionRuntime.lastCaptureError.isNotBlank() -> "Capture error: ${CompanionRuntime.lastCaptureError}"
            CompanionRuntime.captureActive && frame != null -> "Capture: ACTIVE · ${frame.width}×${frame.height} · ${CompanionRuntime.frameCount} frames · light ${frame.meanLuma}/255 · motion ${frame.motionScore}"
            CompanionRuntime.captureActive -> "Capture: starting; waiting for first frame…"
            frame != null -> "Capture: STOPPED · last frame retained in RAM · ${frame.width}×${frame.height}"
            else -> "Capture: OFF · frames stay on this device only"
        }
        if (frame != null && frame.capturedAtMs != previewFrameTime) {
            val pixels = IntArray(frame.grayscale160x90.size) { index ->
                val shade = frame.grayscale160x90[index].toInt() and 0xff
                Color.rgb(shade, shade, shade)
            }
            preview.setImageBitmap(Bitmap.createBitmap(pixels, 160, 90, Bitmap.Config.ARGB_8888))
            previewFrameTime = frame.capturedAtMs
        }
        accessibilityStatus.text = when {
            !MinecraftTouchAccessibilityService.isConnected() -> "Touch service: OFF · enable it in Android Accessibility settings"
            MinecraftTouchAccessibilityService.isMinecraftForeground() -> "Touch service: ENABLED · Minecraft detected · gesture-only · no accessibility-tree reading"
            else -> "Touch service: ENABLED · waiting for Minecraft foreground · gesture-only"
        }
    }

    private fun runBrainSelfCheck() {
        try {
            val report = CompanionBrainBridge.selfCheck()
            brainStatus.text = "Native v0.35 ControlHub: $report"
        } catch (error: UnsatisfiedLinkError) {
            brainStatus.text = "Native C++ brain: library unavailable. Build the companion with the bundled NDK/CMake target. ${error.message}"
        } catch (error: Exception) {
            brainStatus.text = "Native C++ brain check failed: ${error.message}"
        }
    }

    private fun requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), REQUEST_NOTIFICATIONS)
        }
    }

    private fun sectionTitle(text: String) = TextView(this).apply {
        this.text = text
        textSize = 15f
        setTextColor(0xff1d354f.toInt())
        setPadding(0, dp(14), 0, dp(6))
    }

    private fun infoText() = TextView(this).apply {
        textSize = 13f
        setTextColor(0xff34495e.toInt())
        setPadding(0, dp(8), 0, dp(8))
    }

    private fun actionButton(label: String, onClick: () -> Unit) = Button(this).apply {
        text = label
        isAllCaps = false
        setOnClickListener { onClick() }
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            bottomMargin = dp(5)
        }
    }

    private fun numericField(label: String, fraction: Float): EditText = EditText(this).apply {
        hint = "$label (%)"
        setSingleLine(true)
        inputType = android.text.InputType.TYPE_CLASS_NUMBER or android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL
        setText(String.format(Locale.US, "%.0f", fraction * 100f))
        contentDescription = label
    }

    private fun percent(field: EditText, fallback: Float): Float {
        val value = field.text?.toString()?.toFloatOrNull() ?: return fallback
        return (value / 100f).coerceIn(0.03f, 0.98f)
    }

    private fun toast(message: String) = Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    companion object {
        private const val REQUEST_CAPTURE = 8101
        private const val REQUEST_NOTIFICATIONS = 8102
    }
}
