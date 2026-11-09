package com.example.bedrockai

/** JNI-facing handle for the host-owned NetherNet reliable data channel. */
object BedrockAiNetherNetNative {
    init { System.loadLibrary("bedrockai_jni") }

    external fun nativeCreate(): Long
    external fun nativeDestroy(handle: Long)
    external fun nativeConnect(handle: Long): Boolean
    external fun nativeReceive(handle: Long, data: ByteArray): Boolean
    external fun nativeNextOutbound(handle: Long): ByteArray?
    external fun nativePoll(handle: Long): Boolean
}
