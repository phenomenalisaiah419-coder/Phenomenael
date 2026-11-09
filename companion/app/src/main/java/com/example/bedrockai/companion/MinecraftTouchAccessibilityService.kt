package com.example.bedrockai.companion

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.graphics.Path
import android.os.Handler
import android.os.Looper
import android.view.accessibility.AccessibilityEvent
import java.lang.ref.WeakReference

/** Gesture-only service. It does not inspect Minecraft's accessibility tree or window contents. */
class MinecraftTouchAccessibilityService : AccessibilityService() {
    override fun onServiceConnected() {
        super.onServiceConnected()
        synchronized(lock) { instance = WeakReference(this) }
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        if (event?.eventType == AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED) {
            activePackageName = event.packageName?.toString()
        }
    }
    override fun onInterrupt() = Unit

    override fun onDestroy() {
        synchronized(lock) {
            if (instance?.get() === this) instance = null
            activePackageName = null
        }
        super.onDestroy()
    }

    fun dispatch(strokes: List<GestureDescription.StrokeDescription>): Boolean {
        if (strokes.isEmpty()) return false
        val builder = GestureDescription.Builder()
        strokes.forEach { builder.addStroke(it) }
        return dispatchGesture(builder.build(), object : GestureResultCallback() {
            override fun onCompleted(gestureDescription: GestureDescription?) = Unit
            override fun onCancelled(gestureDescription: GestureDescription?) = Unit
        }, Handler(Looper.getMainLooper()))
    }

    companion object {
        private const val MINECRAFT_PACKAGE = "com.mojang.minecraftpe"
        private val lock = Any()
        private var instance: WeakReference<MinecraftTouchAccessibilityService>? = null
        @Volatile private var activePackageName: String? = null

        fun isConnected(): Boolean = synchronized(lock) { instance?.get() != null }
        fun isMinecraftForeground(): Boolean = activePackageName == MINECRAFT_PACKAGE

        fun forward(context: android.content.Context, durationMs: Long = 700L): Boolean {
            val service = synchronized(lock) { instance?.get() } ?: return false
            if (!CompanionRuntime.captureActive || CompanionRuntime.autonomyArmed || !isMinecraftForeground()) return false
            val p = ControlLayout.load(context).pixels(context)
            if (p.width <= p.height || p.forwardY >= p.joystickY) return false
            val path = Path().apply {
                moveTo(p.joystickX, p.joystickY)
                lineTo(p.joystickX, p.forwardY)
            }
            val stroke = GestureDescription.StrokeDescription(path, 0L, durationMs.coerceIn(150L, 1_200L))
            return service.dispatch(listOf(stroke))
        }

        fun lookRight(context: android.content.Context, durationMs: Long = 420L): Boolean {
            val service = synchronized(lock) { instance?.get() } ?: return false
            if (!CompanionRuntime.captureActive || CompanionRuntime.autonomyArmed || !isMinecraftForeground()) return false
            val p = ControlLayout.load(context).pixels(context)
            if (p.width <= p.height || p.lookEndX <= p.lookStartX) return false
            val path = Path().apply {
                moveTo(p.lookStartX, p.lookY)
                lineTo(p.lookEndX, p.lookY)
            }
            val stroke = GestureDescription.StrokeDescription(path, 0L, durationMs.coerceIn(120L, 900L))
            return service.dispatch(listOf(stroke))
        }

        fun forwardAndLookRight(context: android.content.Context, durationMs: Long = 650L): Boolean {
            val service = synchronized(lock) { instance?.get() } ?: return false
            if (!CompanionRuntime.captureActive || CompanionRuntime.autonomyArmed || !isMinecraftForeground()) return false
            val p = ControlLayout.load(context).pixels(context)
            if (p.width <= p.height || p.forwardY >= p.joystickY || p.lookEndX <= p.lookStartX) return false
            val duration = durationMs.coerceIn(150L, 900L)
            val movePath = Path().apply {
                moveTo(p.joystickX, p.joystickY)
                lineTo(p.joystickX, p.forwardY)
            }
            val lookPath = Path().apply {
                moveTo(p.lookStartX, p.lookY)
                lineTo(p.lookEndX, p.lookY)
            }
            return service.dispatch(
                listOf(
                    GestureDescription.StrokeDescription(movePath, 0L, duration),
                    GestureDescription.StrokeDescription(lookPath, 0L, duration)
                )
            )
        }
    }
}
