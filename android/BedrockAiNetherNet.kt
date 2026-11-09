package com.example.bedrockai

/**
 * Host-side bridge for a NetherNet ReliableDataChannel. The Android WebRTC stack
 * owns ICE/DTLS/SCTP and calls these methods from its reliable data-channel callback.
 */
class BedrockAiNetherNetBridge(
    private val nativeHandle: Long,
    private val sendToNative: (Long, ByteArray) -> Unit
) {
    fun onReliableDataChannelMessage(bytes: ByteArray) {
        require(nativeHandle != 0L)
        sendToNative(nativeHandle, bytes)
    }

    fun onDataChannelClosed() {
        // The native lifecycle owner receives network-loss separately; this hook is
        // deliberately side-effect free so a transient channel callback cannot race it.
    }
}
