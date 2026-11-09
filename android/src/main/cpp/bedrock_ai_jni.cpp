#include <jni.h>
#include <cstdint>
#include <new>
#include <vector>

#include "native_controller.hpp"
#include "core/embedded_client_bridge.hpp"
#include "nethernet_host.hpp"
#include "android_jca_crypto_provider.hpp"
#include "core/bedrock_client_integration.hpp"
#include "core/client_process_runtime.hpp"

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeCreate(JNIEnv*, jclass) {
    return static_cast<jlong>(bedrockai_native_create());
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    bedrockai_native_destroy(static_cast<std::uintptr_t>(handle));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeConfigure(JNIEnv*, jclass, jlong handle,
                                                            jint target, jint transport,
                                                            jboolean embeddedClient) {
    return bedrockai_native_configure(static_cast<std::uintptr_t>(handle), target, transport,
                                      embeddedClient == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeCanControlLocal(JNIEnv*, jclass, jlong handle) {
    return bedrockai_native_local_control_capable(static_cast<std::uintptr_t>(handle)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeCanControlRemote(JNIEnv*, jclass, jlong handle) {
    return bedrockai_native_remote_control_capable(static_cast<std::uintptr_t>(handle)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeConnectRakNet(JNIEnv* env, jclass, jlong handle,
                                                               jstring host, jint port) {
    if (!host || port <= 0 || port > 65535) return JNI_FALSE;
    const char* chars = env->GetStringUTFChars(host, nullptr);
    if (!chars) return JNI_FALSE;
    const int ok = bedrockai_native_connect_raknet(static_cast<std::uintptr_t>(handle),
                                                    chars, static_cast<std::uint16_t>(port));
    env->ReleaseStringUTFChars(host, chars);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeConnectSecureRakNet(
    JNIEnv* env, jclass, jlong handle, jstring host, jint port,
    jstring cacheDirectory, jlong cryptoProviderHandle) {
    if (!env || handle == 0 || !host || !cacheDirectory || port <= 0 || port > 65535 ||
        cryptoProviderHandle == 0) return JNI_FALSE;
    const char* host_chars = env->GetStringUTFChars(host, nullptr);
    if (!host_chars) return JNI_FALSE;
    const char* cache_chars = env->GetStringUTFChars(cacheDirectory, nullptr);
    if (!cache_chars) {
        env->ReleaseStringUTFChars(host, host_chars);
        return JNI_FALSE;
    }
    const int ok = bedrockai_native_connect_secure_raknet(
        static_cast<std::uintptr_t>(handle), host_chars, static_cast<std::uint16_t>(port),
        cache_chars, static_cast<std::uintptr_t>(cryptoProviderHandle));
    env->ReleaseStringUTFChars(host, host_chars);
    env->ReleaseStringUTFChars(cacheDirectory, cache_chars);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativePoll(JNIEnv*, jclass, jlong handle) {
    return bedrockai_native_poll(static_cast<std::uintptr_t>(handle)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeLastError(JNIEnv* env, jclass, jlong handle) {
    if (!env) return nullptr;
    const char* message = bedrockai_native_last_error(static_cast<std::uintptr_t>(handle));
    return env->NewStringUTF(message ? message : "");
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeNetworkLost(JNIEnv*, jclass, jlong handle) {
    bedrockai_native_network_lost(static_cast<std::uintptr_t>(handle));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeNetworkAvailable(JNIEnv*, jclass, jlong handle,
                                                                   jlong generation) {
    return bedrockai_native_network_available(static_cast<std::uintptr_t>(handle),
                                              static_cast<std::uint64_t>(generation)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeReconnectRequired(JNIEnv*, jclass, jlong handle) {
    return bedrockai_native_reconnect_required(static_cast<std::uintptr_t>(handle)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_bedrockai_BedrockAiNative_nativeNetworkGeneration(JNIEnv*, jclass, jlong handle) {
    return static_cast<jlong>(bedrockai_native_network_generation(static_cast<std::uintptr_t>(handle)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeCreate(JNIEnv*, jclass) {
    return static_cast<jlong>(bedrockai_client_integration_create());
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    bedrockai_client_integration_destroy(static_cast<std::uintptr_t>(handle));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeCreateProcessRuntime(JNIEnv*, jclass) {
    return static_cast<jlong>(bedrockai_process_runtime_create());
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeDestroyProcessRuntime(JNIEnv*, jclass, jlong handle) {
    bedrockai_process_runtime_destroy(static_cast<std::uintptr_t>(handle));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativePrepareClient(JNIEnv* env, jclass, jlong handle, jstring requiredBuildIdHex) {
    if (!env || handle == 0) return JNI_FALSE;
    const char* build = requiredBuildIdHex ? env->GetStringUTFChars(requiredBuildIdHex, nullptr) : nullptr;
    char error[512]{};
    const int ok = bedrockai_process_runtime_prepare(static_cast<std::uintptr_t>(handle),
                                                       build ? build : "", error, sizeof(error));
    if (build) env->ReleaseStringUTFChars(requiredBuildIdHex, build);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeClientPrepared(JNIEnv*, jclass, jlong handle) {
    return bedrockai_process_runtime_prepared(static_cast<std::uintptr_t>(handle)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeClientProcessId(JNIEnv*, jclass, jlong handle) {
    return static_cast<jlong>(bedrockai_process_runtime_pid(static_cast<std::uintptr_t>(handle)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeClientRuntimeReport(JNIEnv* env, jclass, jlong handle) {
    if (!env || handle == 0) return nullptr;
    char report[4096]{};
    if (bedrockai_process_runtime_report(static_cast<std::uintptr_t>(handle), report, sizeof(report)) == 0) {
        return env->NewStringUTF("");
    }
    return env->NewStringUTF(report);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeAttach(JNIEnv*, jclass, jlong handle) {
    // The actual modified client supplies the native host callback table directly.
    // Java only verifies that the integration object exists; attachment occurs from native game code.
    return handle != 0 ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeDetach(JNIEnv*, jclass, jlong handle) {
    bedrockai_client_integration_detach(static_cast<std::uintptr_t>(handle));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeInstallCompleteHookPlan(JNIEnv* env, jclass, jlong handle, jstring plan) {
    if (!env || handle == 0 || !plan) return JNI_FALSE;
    const char* text = env->GetStringUTFChars(plan, nullptr);
    if (!text) return JNI_FALSE;
    char error[1024]{};
    const int ok = bedrockai_client_integration_install_complete_hook_plan(static_cast<std::uintptr_t>(handle), text, error, sizeof(error));
    env->ReleaseStringUTFChars(plan, text);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeHookKindInstalled(JNIEnv*, jclass, jlong handle, jint kind) {
    if (kind < 0 || kind > 4) return JNI_FALSE;
    return bedrockai_client_integration_hook_kind_installed(static_cast<std::uintptr_t>(handle), static_cast<std::uint8_t>(kind)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeInstallTick(JNIEnv*, jclass, jlong handle) {
    char error[256]{};
    return bedrockai_client_integration_install_tick(static_cast<std::uintptr_t>(handle), error, sizeof(error)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeTick(JNIEnv*, jclass, jlong handle) {
    char error[256]{};
    return bedrockai_client_integration_tick(static_cast<std::uintptr_t>(handle), error, sizeof(error)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeInstallVerifiedTickHook(JNIEnv* env, jclass, jlong handle,
                                                                                  jstring requiredBuildIdHex,
                                                                                  jstring signature,
                                                                                  jstring expected) {
    if (!env || !requiredBuildIdHex || !signature || !expected) return JNI_FALSE;
    const char* build_id = env->GetStringUTFChars(requiredBuildIdHex, nullptr);
    const char* sig = env->GetStringUTFChars(signature, nullptr);
    const char* exp = env->GetStringUTFChars(expected, nullptr);
    if (!build_id || !sig || !exp) {
        if (build_id) env->ReleaseStringUTFChars(requiredBuildIdHex, build_id);
        if (sig) env->ReleaseStringUTFChars(signature, sig);
        if (exp) env->ReleaseStringUTFChars(expected, exp);
        return JNI_FALSE;
    }
    char error[512]{};
    const int ok = bedrockai_client_integration_install_tick_hook(static_cast<std::uintptr_t>(handle),
                                                                   build_id, sig, exp, error, sizeof(error));
    env->ReleaseStringUTFChars(requiredBuildIdHex, build_id);
    env->ReleaseStringUTFChars(signature, sig);
    env->ReleaseStringUTFChars(expected, exp);
    return ok != 0 ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeTickCount(JNIEnv*, jclass, jlong handle) {
    return static_cast<jlong>(bedrockai_client_integration_tick_count(static_cast<std::uintptr_t>(handle)));
}


extern "C" JNIEXPORT jstring JNICALL
Java_com_example_bedrockai_BedrockAiEmbeddedNative_nativeClientImageReport(JNIEnv* env, jclass) {
    char report[4096]{};
    if (bedrockai_current_client_image_report(report, sizeof(report)) == 0) {
        return env->NewStringUTF(report);
    }
    return env->NewStringUTF(report);
}

#if defined(__ANDROID__)
extern "C" JNIEXPORT jlong JNICALL
Java_com_example_bedrockai_BedrockAiCryptoNative_nativeCreate(JNIEnv* env, jclass, jobject provider) {
    auto* crypto = new (std::nothrow) bedrockai::android_host::AndroidJcaCryptoProvider(env, provider);
    if (!crypto || !crypto->valid()) { delete crypto; return 0; }
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(crypto));
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_bedrockai_BedrockAiCryptoNative_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    delete reinterpret_cast<bedrockai::android_host::AndroidJcaCryptoProvider*>(static_cast<std::uintptr_t>(handle));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_bedrockai_BedrockAiNetherNetNative_nativeCreate(JNIEnv*, jclass) {
    return static_cast<jlong>(bedrockai_nethernet_create());
}
extern "C" JNIEXPORT void JNICALL
Java_com_example_bedrockai_BedrockAiNetherNetNative_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    bedrockai_nethernet_destroy(static_cast<std::uintptr_t>(handle));
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiNetherNetNative_nativeConnect(JNIEnv*, jclass, jlong handle) {
    return bedrockai_nethernet_connect(static_cast<std::uintptr_t>(handle)) ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiNetherNetNative_nativeReceive(JNIEnv* env, jclass, jlong handle, jbyteArray data) {
    if (!data) return JNI_FALSE;
    const jsize len = env->GetArrayLength(data);
    if (len <= 0) return JNI_FALSE;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(len));
    env->GetByteArrayRegion(data, 0, len, reinterpret_cast<jbyte*>(bytes.data()));
    if (env->ExceptionCheck()) { env->ExceptionClear(); return JNI_FALSE; }
    return bedrockai_nethernet_receive(static_cast<std::uintptr_t>(handle), bytes.data(), bytes.size()) ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_example_bedrockai_BedrockAiNetherNetNative_nativeNextOutbound(JNIEnv* env, jclass, jlong handle) {
    const std::size_t len = bedrockai_nethernet_next_outbound_size(static_cast<std::uintptr_t>(handle));
    if (len == 0) return nullptr;
    jbyteArray out = env->NewByteArray(static_cast<jsize>(len));
    if (!out) return nullptr;
    std::vector<std::uint8_t> bytes(len);
    const std::size_t copied = bedrockai_nethernet_next_outbound(static_cast<std::uintptr_t>(handle), bytes.data(), bytes.size());
    if (copied != len) { env->DeleteLocalRef(out); return nullptr; }
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(copied), reinterpret_cast<const jbyte*>(bytes.data()));
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(out); return nullptr; }
    return out;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bedrockai_BedrockAiNetherNetNative_nativePoll(JNIEnv*, jclass, jlong handle) {
    return bedrockai_nethernet_poll(static_cast<std::uintptr_t>(handle)) ? JNI_TRUE : JNI_FALSE;
}
#endif
