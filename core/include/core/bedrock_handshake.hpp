#pragma once

#include "bedrock_crypto.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bedrockai {

struct ServerEncryptionHandshake {
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> server_public_key_pkix_der;
    std::array<std::uint8_t, 32> encryption_key{};
};

class BedrockServerToClientHandshakeProcessor {
public:
    explicit BedrockServerToClientHandshakeProcessor(BedrockCryptoProvider& crypto)
        : crypto_(crypto) {}

    bool process(const std::vector<std::uint8_t>& jwt,
                 const std::vector<std::uint8_t>& client_private_key_pkcs8_der,
                 ServerEncryptionHandshake& out,
                 std::string* error = nullptr) const;

private:
    BedrockCryptoProvider& crypto_;

    static bool base64_decode_flexible(const std::string& input,
                                       std::vector<std::uint8_t>& output,
                                       std::string* error);
    static bool extract_json_string_field(const std::string& json,
                                          const std::string& field,
                                          std::string& value,
                                          std::string* error);
    static bool split_compact_jwt(const std::string& jwt,
                                  std::string& header_b64,
                                  std::string& payload_b64,
                                  std::string& signature_b64,
                                  std::string* error);
    static bool base64url_decode(const std::string& input,
                                 std::vector<std::uint8_t>& output,
                                 std::string* error);
};

} // namespace bedrockai
