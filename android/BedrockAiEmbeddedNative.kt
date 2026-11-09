package com.example.bedrockai

/** JNI boundary used by the modified Bedrock client process.
 * The game-side native integration calls the C ABI directly at its real tick/state/input sites.
 */
object BedrockAiEmbeddedNative {
    init { System.loadLibrary("bedrockai_jni") }
    external fun nativeCreate(): Long
    external fun nativeDestroy(handle: Long)
    external fun nativeAttach(handle: Long): Boolean
    external fun nativeCreateProcessRuntime(): Long
    external fun nativeDestroyProcessRuntime(handle: Long)
    external fun nativePrepareClient(handle: Long, requiredBuildIdHex: String = ""): Boolean
    external fun nativeClientPrepared(handle: Long): Boolean
    external fun nativeClientProcessId(handle: Long): Long
    external fun nativeClientRuntimeReport(handle: Long): String
    external fun nativeDetach(handle: Long)
    external fun nativeInstallTick(handle: Long): Boolean
    external fun nativeInstallCompleteHookPlan(handle: Long, plan: String): Boolean
    external fun nativeHookKindInstalled(handle: Long, kind: Int): Boolean
    external fun nativeInstallVerifiedTickHook(handle: Long, requiredBuildIdHex: String, signature: String, expected: String): Boolean
    external fun nativeTick(handle: Long): Boolean
    external fun nativeTickCount(handle: Long): Long
    external fun nativeClientImageReport(): String
}
