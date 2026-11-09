#pragma once

#include "core/bedrock_crypto.hpp"

#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace bedrockai::android_host {

#if defined(__ANDROID__)
class AndroidJcaCryptoProvider final : public BedrockCryptoProvider {
public:
    AndroidJcaCryptoProvider(JNIEnv* env, jobject provider);
    ~AndroidJcaCryptoProvider() override;

    AndroidJcaCryptoProvider(const AndroidJcaCryptoProvider&) = delete;
    AndroidJcaCryptoProvider& operator=(const AndroidJcaCryptoProvider&) = delete;

    bool valid() const { return provider_global_ != nullptr && vm_ != nullptr; }

    bool generate_p384_keypair(EcKeyPairDer& out, std::string* error = nullptr) override;
    bool sign_es384(const std::vector<std::uint8_t>& private_key_pkcs8_der,
                    const std::vector<std::uint8_t>& signing_input,
                    std::vector<std::uint8_t>& signature_raw_rs,
                    std::string* error = nullptr) override;
    bool verify_es384(const std::vector<std::uint8_t>& public_key_pkix_der,
                      const std::vector<std::uint8_t>& signing_input,
                      const std::vector<std::uint8_t>& signature_raw_rs,
                      std::string* error = nullptr) override;
    bool p384_shared_secret_x(const std::vector<std::uint8_t>& private_key_pkcs8_der,
                              const std::vector<std::uint8_t>& peer_public_key_pkix_der,
                              std::vector<std::uint8_t>& shared_secret_x48,
                              std::string* error = nullptr) override;
    bool sha256(const std::vector<std::uint8_t>& data,
                std::array<std::uint8_t, 32>& digest,
                std::string* error = nullptr) override;

private:
    JavaVM* vm_ = nullptr;
    jobject provider_global_ = nullptr;
    jclass provider_class_global_ = nullptr;
    jmethodID generate_method_ = nullptr;
    jmethodID sign_method_ = nullptr;
    jmethodID verify_method_ = nullptr;
    jmethodID ecdh_method_ = nullptr;
    jmethodID sha256_method_ = nullptr;

    bool init(JNIEnv* env, jobject provider, std::string* error);
    JNIEnv* env_for_call(bool& attached, std::string* error) const;
    static bool clear_exception(JNIEnv* env, std::string* error, const char* operation);
    static jbyteArray to_java_bytes(JNIEnv* env, const std::vector<std::uint8_t>& data);
    static bool from_java_bytes(JNIEnv* env, jbyteArray data, std::vector<std::uint8_t>& out, std::string* error);
};
#endif

} // namespace bedrockai::android_host
