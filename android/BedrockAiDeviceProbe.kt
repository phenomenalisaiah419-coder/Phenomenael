package com.example.bedrockai

/** Reads the exact loaded Bedrock ARM64 library identity before any hook is allowed. */
object BedrockAiDeviceProbe {
    fun clientImageReport(): String = BedrockAiEmbeddedNative.nativeClientImageReport()
}
