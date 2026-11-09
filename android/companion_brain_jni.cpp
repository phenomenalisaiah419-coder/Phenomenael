#include <jni.h>

#include "companion_brain_adapter.hpp"

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_bedrockai_companion_BedrockCompanionNative_nativeSelfCheck(JNIEnv* env, jobject) {
    if (env == nullptr) return nullptr;
    const std::string report = bedrockai::companion::run_native_safety_self_check();
    return env->NewStringUTF(report.c_str());
}
