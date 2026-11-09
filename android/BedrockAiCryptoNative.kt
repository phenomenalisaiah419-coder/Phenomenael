package com.example.bedrockai

/** JNI lifecycle for the Android platform JCA crypto provider. */
object BedrockAiCryptoNative {
    init { System.loadLibrary("bedrockai_jni") }
    external fun nativeCreate(provider: AndroidJcaCrypto): Long
    external fun nativeDestroy(handle: Long)
}
