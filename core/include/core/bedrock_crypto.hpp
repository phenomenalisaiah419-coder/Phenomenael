#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bedrockai {

struct EcKeyPairDer {
    std::vector<std::uint8_t> private_key_pkcs8_der;
    std::vector<std::uint8_t> public_key_pkix_der;
};

struct BedrockPacketCryptoState {
    bool enabled = false;
    std::array<std::uint8_t, 32> key{};
};

class BedrockCryptoProvider {
public:
    virtual ~BedrockCryptoProvider() = default;

    virtual bool generate_p384_keypair(EcKeyPairDer& out, std::string* error = nullptr) = 0;
    virtual bool sign_es384(const std::vector<std::uint8_t>& private_key_pkcs8_der,
                            const std::vector<std::uint8_t>& signing_input,
                            std::vector<std::uint8_t>& signature_raw_rs,
                            std::string* error = nullptr) = 0;
    virtual bool verify_es384(const std::vector<std::uint8_t>& public_key_pkix_der,
                              const std::vector<std::uint8_t>& signing_input,
                              const std::vector<std::uint8_t>& signature_raw_rs,
                              std::string* error = nullptr) = 0;
    virtual bool p384_shared_secret_x(const std::vector<std::uint8_t>& private_key_pkcs8_der,
                                      const std::vector<std::uint8_t>& peer_public_key_pkix_der,
                                      std::vector<std::uint8_t>& shared_secret_x48,
                                      std::string* error = nullptr) = 0;
    virtual bool sha256(const std::vector<std::uint8_t>& data,
                        std::array<std::uint8_t, 32>& digest,
                        std::string* error = nullptr) = 0;
};

// OpenSSL implementation used by desktop/Linux/CI. Other hosts may inject a provider
// implemented with Android/NDK-compatible crypto without changing the protocol layer.
class OpenSSLBedrockCryptoProvider final : public BedrockCryptoProvider {
public:
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
};

class BedrockPacketCipher {
public:
    BedrockPacketCipher();
    ~BedrockPacketCipher();
    BedrockPacketCipher(BedrockPacketCipher&&) noexcept;
    BedrockPacketCipher& operator=(BedrockPacketCipher&&) noexcept;
    BedrockPacketCipher(const BedrockPacketCipher&) = delete;
    BedrockPacketCipher& operator=(const BedrockPacketCipher&) = delete;

    bool enable(const std::array<std::uint8_t, 32>& key, std::string* error = nullptr);
    bool enabled() const { return state_.enabled; }
    void disable();

    // Bedrock's current packet encryption path keeps the first packet byte (0xfe/packet id)
    // plaintext, appends an 8-byte SHA-256 checksum, and AES-256-CTR encrypts bytes after byte 0.
    bool encrypt_packet(std::vector<std::uint8_t>& packet, std::string* error = nullptr);
    bool decrypt_packet(std::vector<std::uint8_t>& packet, std::string* error = nullptr);

    std::uint64_t send_counter() const { return send_counter_; }
    std::uint64_t receive_counter() const { return receive_counter_; }
    const BedrockPacketCryptoState& state() const { return state_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BedrockPacketCryptoState state_{};
    std::uint64_t send_counter_ = 0;
    std::uint64_t receive_counter_ = 0;
};

} // namespace bedrockai
