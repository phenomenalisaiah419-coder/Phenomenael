package com.example.bedrockai.companion

import android.content.Context
import android.view.Display
import kotlin.math.roundToInt

data class ControlLayout(
    val joystickX: Float = 0.14f,
    val joystickY: Float = 0.82f,
    val forwardY: Float = 0.71f,
    val lookStartX: Float = 0.70f,
    val lookEndX: Float = 0.86f,
    val lookY: Float = 0.63f
) {
    fun pixels(context: Context): PixelControlLayout {
        val display = DisplayGeometry.read(context)
        return PixelControlLayout(
            width = display.width,
            height = display.height,
            joystickX = (joystickX.coerceIn(0.03f, 0.45f) * display.width).roundToInt().toFloat(),
            joystickY = (joystickY.coerceIn(0.50f, 0.96f) * display.height).roundToInt().toFloat(),
            forwardY = (forwardY.coerceIn(0.40f, 0.94f) * display.height).roundToInt().toFloat(),
            lookStartX = (lookStartX.coerceIn(0.50f, 0.90f) * display.width).roundToInt().toFloat(),
            lookEndX = (lookEndX.coerceIn(0.55f, 0.98f) * display.width).roundToInt().toFloat(),
            lookY = (lookY.coerceIn(0.35f, 0.94f) * display.height).roundToInt().toFloat()
        )
    }

    fun save(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putFloat("joy_x", joystickX)
            .putFloat("joy_y", joystickY)
            .putFloat("forward_y", forwardY)
            .putFloat("look_start_x", lookStartX)
            .putFloat("look_end_x", lookEndX)
            .putFloat("look_y", lookY)
            .apply()
    }

    companion object {
        private const val PREFS = "bedrock_companion_controls"
        fun load(context: Context): ControlLayout {
            val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            return ControlLayout(
                joystickX = p.getFloat("joy_x", 0.14f),
                joystickY = p.getFloat("joy_y", 0.82f),
                forwardY = p.getFloat("forward_y", 0.71f),
                lookStartX = p.getFloat("look_start_x", 0.70f),
                lookEndX = p.getFloat("look_end_x", 0.86f),
                lookY = p.getFloat("look_y", 0.63f)
            )
        }
    }
}

data class PixelControlLayout(
    val width: Int,
    val height: Int,
    val joystickX: Float,
    val joystickY: Float,
    val forwardY: Float,
    val lookStartX: Float,
    val lookEndX: Float,
    val lookY: Float
)
