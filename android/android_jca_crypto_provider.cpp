#include "android_jca_crypto_provider.hpp"

#if defined(__ANDROID__)
#include <algorithm>

namespace bedrockai::android_host {
namespace {
void set_error(std::string* error, const char* message) {
    if (error) *error = message;
}
}

AndroidJcaCryptoProvider::AndroidJcaCryptoProvider(JNIEnv* env, jobject provider) {
    std::string error;
    (void)init(env, provider, &error);
}

AndroidJcaCryptoProvider::~AndroidJcaCryptoProvider() {
    if (vm_ == nullptr) return;
    bool attached = false;
    JNIEnv* env = nullptr;
    if (vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (vm_->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) return;
        attached = true;
    }
    if (env != nullptr) {
        if (provider_global_ != nullptr) env->DeleteGlobalRef(provider_global_);
        if (provider_class_global_ != nullptr) env->DeleteGlobalRef(provider_class_global_);
    }
    if (attached) vm_->DetachCurrentThread();
}

bool AndroidJcaCryptoProvider::init(JNIEnv* env, jobject provider, std::string* error) {
    if (env == nullptr || provider == nullptr) {
        set_error(error, "Android JCA crypto provider requires a non-null Java provider");
        return false;
    }
    if (env->GetJavaVM(&vm_) != JNI_OK) {
        set_error(error, "unable to obtain JavaVM");
        vm_ = nullptr;
        return false;
    }
    provider_global_ = env->NewGlobalRef(provider);
    if (provider_global_ == nullptr) {
        set_error(error, "unable to create Android crypto provider global reference");
        return false;
    }
    jclass local_class = env->GetObjectClass(provider);
    if (local_class == nullptr) {
        set_error(error, "unable to obtain Android crypto provider class");
        return false;
    }
    provider_class_global_ = reinterpret_cast<jclass>(env->NewGlobalRef(local_class));
    env->DeleteLocalRef(local_class);
    if (provider_class_global_ == nullptr) {
        set_error(error, "unable to create Android crypto provider class reference");
        return false;
    }

    generate_method_ = env->GetMethodID(provider_class_global_, "generateP384KeyPair", "()[[B");
    sign_method_ = env->GetMethodID(provider_class_global_, "signEs384", "([B[B)[B");
    verify_method_ = env->GetMethodID(provider_class_global_, "verifyEs384", "([B[B[B)Z");
    ecdh_method_ = env->GetMethodID(provider_class_global_, "deriveP384SharedSecretX", "([B[B)[B");
    sha256_method_ = env->GetMethodID(provider_class_global_, "sha256", "([B)[B");
    if (generate_method_ == nullptr || sign_method_ == nullptr || verify_method_ == nullptr ||
        ecdh_method_ == nullptr || sha256_method_ == nullptr) {
        clear_exception(env, error, "resolve Android crypto provider methods");
        set_error(error, "Android JCA crypto provider method lookup failed");
        return false;
    }
    return true;
}

JNIEnv* AndroidJcaCryptoProvider::env_for_call(bool& attached, std::string* error) const {
    attached = false;
    if (vm_ == nullptr) {
        set_error(error, "Android JCA provider has no JavaVM");
        return nullptr;
    }
    JNIEnv* env = nullptr;
    const jint rc = vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (rc == JNI_OK) return env;
    if (rc != JNI_EDETACHED || vm_->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) {
        set_error(error, "unable to attach current thread to Android VM");
        return nullptr;
    }
    attached = true;
    return env;
}

bool AndroidJcaCryptoProvider::clear_exception(JNIEnv* env, std::string* error, const char* operation) {
    if (env == nullptr || !env->ExceptionCheck()) return true;
    env->ExceptionClear();
    if (error) *error = std::string("Android JCA exception during ") + operation;
    return false;
}

jbyteArray AndroidJcaCryptoProvider::to_java_bytes(JNIEnv* env, const std::vector<std::uint8_t>& data) {
    const auto length = static_cast<jsize>(data.size());
    jbyteArray result = env->NewByteArray(length);
    if (result == nullptr) return nullptr;
    if (length > 0) {
        env->SetByteArrayRegion(result, 0, length,
                                reinterpret_cast<const jbyte*>(data.data()));
    }
    return result;
}

bool AndroidJcaCryptoProvider::from_java_bytes(JNIEnv* env, jbyteArray data,
                                                std::vector<std::uint8_t>& out,
                                                std::string* error) {
    if (data == nullptr) {
        set_error(error, "Android JCA provider returned null byte array");
        return false;
    }
    const jsize length = env->GetArrayLength(data);
    if (length < 0) {
        set_error(error, "Android JCA provider returned invalid byte-array length");
        return false;
    }
    out.resize(static_cast<std::size_t>(length));
    if (length > 0) {
        env->GetByteArrayRegion(data, 0, length, reinterpret_cast<jbyte*>(out.data()));
        if (!clear_exception(env, error, "read Android crypto byte array")) return false;
    }
    return true;
}

bool AndroidJcaCryptoProvider::generate_p384_keypair(EcKeyPairDer& out, std::string* error) {
    bool attached = false;
    JNIEnv* env = env_for_call(attached, error);
    if (!env) return false;
    jobjectArray pair = static_cast<jobjectArray>(env->CallObjectMethod(provider_global_, generate_method_));
    if (!clear_exception(env, error, "generate P-384 key pair")) { if (attached) vm_->DetachCurrentThread(); return false; }
    if (pair == nullptr || env->GetArrayLength(pair) != 2) {
        set_error(error, "Android crypto provider returned an invalid key pair");
        if (pair) env->DeleteLocalRef(pair);
        if (attached) vm_->DetachCurrentThread();
        return false;
    }
    auto priv = static_cast<jbyteArray>(env->GetObjectArrayElement(pair, 0));
    auto pub = static_cast<jbyteArray>(env->GetObjectArrayElement(pair, 1));
    std::vector<std::uint8_t> priv_bytes;
    std::vector<std::uint8_t> pub_bytes;
    const bool ok = from_java_bytes(env, priv, priv_bytes, error) && from_java_bytes(env, pub, pub_bytes, error);
    if (priv) env->DeleteLocalRef(priv);
    if (pub) env->DeleteLocalRef(pub);
    env->DeleteLocalRef(pair);
    if (attached) vm_->DetachCurrentThread();
    if (!ok) return false;
    out.private_key_pkcs8_der = std::move(priv_bytes);
    out.public_key_pkix_der = std::move(pub_bytes);
    return !out.private_key_pkcs8_der.empty() && !out.public_key_pkix_der.empty();
}

bool AndroidJcaCryptoProvider::sign_es384(const std::vector<std::uint8_t>& private_key_pkcs8_der,
                                          const std::vector<std::uint8_t>& signing_input,
                                          std::vector<std::uint8_t>& signature_raw_rs,
                                          std::string* error) {
    bool attached = false;
    JNIEnv* env = env_for_call(attached, error);
    if (!env) return false;
    jbyteArray priv = to_java_bytes(env, private_key_pkcs8_der);
    jbyteArray data = to_java_bytes(env, signing_input);
    if (!priv || !data) {
        set_error(error, "unable to allocate Android signing byte arrays");
        if (priv) env->DeleteLocalRef(priv); if (data) env->DeleteLocalRef(data);
        if (attached) vm_->DetachCurrentThread();
        return false;
    }
    auto sig = static_cast<jbyteArray>(env->CallObjectMethod(provider_global_, sign_method_, priv, data));
    env->DeleteLocalRef(priv); env->DeleteLocalRef(data);
    if (!clear_exception(env, error, "ES384 signing")) { if (sig) env->DeleteLocalRef(sig); if (attached) vm_->DetachCurrentThread(); return false; }
    const bool ok = from_java_bytes(env, sig, signature_raw_rs, error);
    if (sig) env->DeleteLocalRef(sig);
    if (attached) vm_->DetachCurrentThread();
    return ok && signature_raw_rs.size() == 96;
}

bool AndroidJcaCryptoProvider::verify_es384(const std::vector<std::uint8_t>& public_key_pkix_der,
                                            const std::vector<std::uint8_t>& signing_input,
                                            const std::vector<std::uint8_t>& signature_raw_rs,
                                            std::string* error) {
    if (signature_raw_rs.size() != 96) { set_error(error, "ES384 signature must be exactly 96 bytes"); return false; }
    bool attached = false;
    JNIEnv* env = env_for_call(attached, error);
    if (!env) return false;
    jbyteArray pub = to_java_bytes(env, public_key_pkix_der);
    jbyteArray data = to_java_bytes(env, signing_input);
    jbyteArray sig = to_java_bytes(env, signature_raw_rs);
    if (!pub || !data || !sig) {
        set_error(error, "unable to allocate Android verification byte arrays");
        if (pub) env->DeleteLocalRef(pub); if (data) env->DeleteLocalRef(data); if (sig) env->DeleteLocalRef(sig);
        if (attached) vm_->DetachCurrentThread();
        return false;
    }
    const jboolean result = env->CallBooleanMethod(provider_global_, verify_method_, pub, data, sig);
    env->DeleteLocalRef(pub); env->DeleteLocalRef(data); env->DeleteLocalRef(sig);
    if (!clear_exception(env, error, "ES384 verification")) { if (attached) vm_->DetachCurrentThread(); return false; }
    if (attached) vm_->DetachCurrentThread();
    if (!result) set_error(error, "Android JCA ES384 signature verification failed");
    return result == JNI_TRUE;
}

bool AndroidJcaCryptoProvider::p384_shared_secret_x(const std::vector<std::uint8_t>& private_key_pkcs8_der,
                                                    const std::vector<std::uint8_t>& peer_public_key_pkix_der,
                                                    std::vector<std::uint8_t>& shared_secret_x48,
                                                    std::string* error) {
    bool attached = false;
    JNIEnv* env = env_for_call(attached, error);
    if (!env) return false;
    jbyteArray priv = to_java_bytes(env, private_key_pkcs8_der);
    jbyteArray pub = to_java_bytes(env, peer_public_key_pkix_der);
    if (!priv || !pub) {
        set_error(error, "unable to allocate Android ECDH byte arrays");
        if (priv) env->DeleteLocalRef(priv); if (pub) env->DeleteLocalRef(pub);
        if (attached) vm_->DetachCurrentThread();
        return false;
    }
    auto shared = static_cast<jbyteArray>(env->CallObjectMethod(provider_global_, ecdh_method_, priv, pub));
    env->DeleteLocalRef(priv); env->DeleteLocalRef(pub);
    if (!clear_exception(env, error, "P-384 ECDH")) { if (shared) env->DeleteLocalRef(shared); if (attached) vm_->DetachCurrentThread(); return false; }
    const bool ok = from_java_bytes(env, shared, shared_secret_x48, error);
    if (shared) env->DeleteLocalRef(shared);
    if (attached) vm_->DetachCurrentThread();
    return ok && shared_secret_x48.size() == 48;
}

bool AndroidJcaCryptoProvider::sha256(const std::vector<std::uint8_t>& data,
                                      std::array<std::uint8_t, 32>& digest,
                                      std::string* error) {
    bool attached = false;
    JNIEnv* env = env_for_call(attached, error);
    if (!env) return false;
    jbyteArray input = to_java_bytes(env, data);
    if (!input) { set_error(error, "unable to allocate Android SHA-256 byte array"); if (attached) vm_->DetachCurrentThread(); return false; }
    auto output = static_cast<jbyteArray>(env->CallObjectMethod(provider_global_, sha256_method_, input));
    env->DeleteLocalRef(input);
    if (!clear_exception(env, error, "SHA-256")) { if (output) env->DeleteLocalRef(output); if (attached) vm_->DetachCurrentThread(); return false; }
    std::vector<std::uint8_t> bytes;
    const bool ok = from_java_bytes(env, output, bytes, error);
    if (output) env->DeleteLocalRef(output);
    if (attached) vm_->DetachCurrentThread();
    if (!ok || bytes.size() != digest.size()) { set_error(error, "Android SHA-256 provider returned invalid digest size"); return false; }
    std::copy(bytes.begin(), bytes.end(), digest.begin());
    return true;
}

} // namespace bedrockai::android_host
#endif
