package com.example.bedrockai.companion

import android.content.Context
import android.os.Build
import android.util.DisplayMetrics
import android.view.WindowManager

/** Full display bounds, not the companion activity's potentially smaller multi-window bounds. */
data class DisplayGeometry(val width: Int, val height: Int, val densityDpi: Int) {
    companion object {
        fun read(context: Context): DisplayGeometry {
            val windowManager = context.getSystemService(WindowManager::class.java)
                ?: error("WindowManager is unavailable")
            val density = context.resources.configuration.densityDpi.coerceAtLeast(1)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val bounds = windowManager.maximumWindowMetrics.bounds
                return DisplayGeometry(bounds.width().coerceAtLeast(1), bounds.height().coerceAtLeast(1), density)
            }
            val metrics = DisplayMetrics()
            @Suppress("DEPRECATION")
            windowManager.defaultDisplay.getRealMetrics(metrics)
            return DisplayGeometry(metrics.widthPixels.coerceAtLeast(1), metrics.heightPixels.coerceAtLeast(1), density)
        }
    }
}
