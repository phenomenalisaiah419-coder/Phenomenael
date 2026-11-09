package com.example.bedrockai.companion

import android.content.Context
import android.graphics.Color
import android.graphics.PixelFormat
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

/** Small always-visible test/panic panel. It never starts autonomous behaviour. */
class PanicOverlay(
    private val context: Context,
    private val onStop: () -> Unit
) {
    private val windowManager = context.getSystemService(WindowManager::class.java)
    private var root: LinearLayout? = null

    fun show() {
        if (root != null || windowManager == null) return
        val panel = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(8), dp(6), dp(8), dp(6))
            background = GradientDrawable().apply {
                setColor(0xE6222730.toInt())
                cornerRadius = dp(18).toFloat()
                setStroke(dp(1), 0xFF667080.toInt())
            }
        }
        val label = TextView(context).apply {
            text = "BEDROCK · TEST"
            setTextColor(Color.WHITE)
            textSize = 11f
            setPadding(dp(4), 0, dp(8), 0)
        }
        val forward = Button(context).apply {
            text = "FWD"
            textSize = 10f
            setOnClickListener {
                if (!MinecraftTouchAccessibilityService.forward(context, 650L)) {
                    label.text = "ACCESSIBILITY OFF"
                } else label.text = "MOVE TEST"
            }
        }
        val turn = Button(context).apply {
            text = "TURN"
            textSize = 10f
            setOnClickListener {
                if (!MinecraftTouchAccessibilityService.lookRight(context, 350L)) {
                    label.text = "ACCESSIBILITY OFF"
                } else label.text = "LOOK TEST"
            }
        }
        val stop = Button(context).apply {
            text = "STOP"
            textSize = 10f
            setTextColor(Color.WHITE)
            setBackgroundColor(0xFFC62828.toInt())
            setOnClickListener { onStop() }
        }
        panel.addView(label)
        panel.addView(forward)
        panel.addView(turn)
        panel.addView(stop)

        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.END
            x = dp(8)
            y = dp(56)
        }
        try {
            windowManager.addView(panel, params)
            root = panel
        } catch (_: Exception) {
            root = null
        }
    }

    fun hide() {
        root?.let { view ->
            try { windowManager?.removeView(view) } catch (_: Exception) { }
        }
        root = null
    }

    private fun dp(value: Int): Int = (value * context.resources.displayMetrics.density).toInt()
}
