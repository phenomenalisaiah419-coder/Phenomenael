package com.example.bedrockai.companion

import android.media.Image
import android.os.SystemClock
import java.nio.ByteOrder
import kotlin.math.abs

/** Downsamples a frame to 160x90 grayscale for a future perception stage; no image is persisted. */
object ScreenFrameAnalyzer {
    private const val SAMPLE_WIDTH = 160
    private const val SAMPLE_HEIGHT = 90
    @Volatile private var previous: ByteArray? = null

    fun analyze(image: Image): ScreenFrameSnapshot? {
        if (image.planes.isEmpty()) return null
        val plane = image.planes[0]
        val pixelStride = plane.pixelStride
        val rowStride = plane.rowStride
        if (pixelStride < 4 || rowStride <= 0 || image.width <= 0 || image.height <= 0) return null

        val buffer = plane.buffer.duplicate().order(ByteOrder.nativeOrder())
        val sample = ByteArray(SAMPLE_WIDTH * SAMPLE_HEIGHT)
        var total = 0L
        var center = 0L
        var centerCount = 0L
        val centerLeft = SAMPLE_WIDTH * 2 / 5
        val centerRight = SAMPLE_WIDTH * 3 / 5
        val centerTop = SAMPLE_HEIGHT * 2 / 5
        val centerBottom = SAMPLE_HEIGHT * 3 / 5

        for (sy in 0 until SAMPLE_HEIGHT) {
            val sourceY = ((sy.toLong() * image.height) / SAMPLE_HEIGHT).toInt().coerceAtMost(image.height - 1)
            for (sx in 0 until SAMPLE_WIDTH) {
                val sourceX = ((sx.toLong() * image.width) / SAMPLE_WIDTH).toInt().coerceAtMost(image.width - 1)
                val offset = sourceY * rowStride + sourceX * pixelStride
                if (offset < 0 || offset + 2 >= buffer.limit()) return null
                val red = buffer.get(offset).toInt() and 0xff
                val green = buffer.get(offset + 1).toInt() and 0xff
                val blue = buffer.get(offset + 2).toInt() and 0xff
                val luma = ((77 * red + 150 * green + 29 * blue) shr 8).coerceIn(0, 255)
                sample[sy * SAMPLE_WIDTH + sx] = luma.toByte()
                total += luma.toLong()
                if (sx in centerLeft until centerRight && sy in centerTop until centerBottom) {
                    center += luma.toLong()
                    centerCount++
                }
            }
        }

        val old = previous
        var difference = 0L
        if (old != null && old.size == sample.size) {
            for (i in sample.indices step 4) {
                difference += abs((sample[i].toInt() and 0xff) - (old[i].toInt() and 0xff)).toLong()
            }
        }
        previous = sample
        val sampledCount = (sample.size / 4).coerceAtLeast(1)
        return ScreenFrameSnapshot(
            width = image.width,
            height = image.height,
            capturedAtMs = SystemClock.elapsedRealtime(),
            meanLuma = (total / sample.size).toInt(),
            centerLuma = if (centerCount > 0L) (center / centerCount).toInt() else 0,
            motionScore = if (old == null) 0 else (difference / sampledCount).toInt(),
            grayscale160x90 = sample
        )
    }

    fun reset() {
        previous = null
    }
}
