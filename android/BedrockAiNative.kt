package com.example.bedrockai

/** Stable JNI-facing lifecycle seam for the embedded controller runtime. */
object BedrockAiNative {
    init { System.loadLibrary("bedrockai_jni") }

    external fun nativeCreate(): Long
    external fun nativeDestroy(handle: Long)
    external fun nativeConfigure(handle: Long, target: Int, transport: Int, embeddedClient: Boolean): Boolean
    external fun nativeCanControlLocal(handle: Long): Boolean
    external fun nativeCanControlRemote(handle: Long): Boolean
    external fun nativeConnectRakNet(handle: Long, host: String, port: Int): Boolean
    /** Starts authenticated Bedrock login using an app-private imported cache and live JCA crypto provider. */
    external fun nativeConnectSecureRakNet(
        handle: Long,
        host: String,
        port: Int,
        cacheDirectory: String,
        cryptoProviderHandle: Long
    ): Boolean
    external fun nativePoll(handle: Long): Boolean
    external fun nativeLastError(handle: Long): String
    external fun nativeNetworkLost(handle: Long)
    external fun nativeNetworkAvailable(handle: Long, generation: Long): Boolean
    external fun nativeReconnectRequired(handle: Long): Boolean
    external fun nativeNetworkGeneration(handle: Long): Long

    const val TARGET_LOCAL_WORLD = 0
    const val TARGET_LAN_WORLD = 1
    const val TARGET_REALM = 2
    const val TARGET_REMOTE_SERVER = 3

    const val TRANSPORT_AUTO = 0
    const val TRANSPORT_RAKNET = 1
    const val TRANSPORT_NETHERNET = 2
}
