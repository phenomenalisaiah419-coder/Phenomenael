package com.example.bedrockai.companion

/** JNI bridge to the v0.35 shared-control hub. Stage 1 exposes only a safe self-check. */
object BedrockCompanionNative {
    init {
        System.loadLibrary("bedrockai_jni")
    }

    external fun nativeSelfCheck(): String
}
