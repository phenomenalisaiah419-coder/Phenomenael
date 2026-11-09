package com.example.bedrockai

/**
 * Minimal JNI-facing host contract. The implementation belongs to the Android host
 * integration, not to the AI policy layer.
 */
interface HostBridge {
    fun isBedrockClientPresent(): Boolean
    fun onControllerReady()
    fun onControllerError(message: String)
    fun submitPlayerAuthInput(frame: ByteArray): Boolean
    fun submitInventoryAttack(frame: ByteArray): Boolean
}
