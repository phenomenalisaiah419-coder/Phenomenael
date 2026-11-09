package com.example.bedrockai.companion

import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/** A small, in-memory frame handoff. Captured pixels are never written to disk or sent over a network. */
data class ScreenFrameSnapshot(
    val width: Int,
    val height: Int,
    val capturedAtMs: Long,
    val meanLuma: Int,
    val centerLuma: Int,
    val motionScore: Int,
    val grayscale160x90: ByteArray
)

object CompanionRuntime {
    private val latestFrameRef = AtomicReference<ScreenFrameSnapshot?>(null)
    private val frameCountRef = AtomicLong(0L)

    @Volatile var captureActive: Boolean = false
    @Volatile var autonomyArmed: Boolean = false
    @Volatile var lastCaptureError: String = ""

    val latestFrame: ScreenFrameSnapshot? get() = latestFrameRef.get()
    val frameCount: Long get() = frameCountRef.get()

    fun publishFrame(frame: ScreenFrameSnapshot) {
        latestFrameRef.set(frame)
        frameCountRef.incrementAndGet()
        lastCaptureError = ""
    }

    fun clearFrames() {
        latestFrameRef.set(null)
        frameCountRef.set(0L)
    }

    fun panicStop() {
        autonomyArmed = false
    }
}
