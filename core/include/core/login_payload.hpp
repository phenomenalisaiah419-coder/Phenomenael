#pragma once

#include "bedrock_crypto.hpp"
#include "protocol_adapter.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <functional>

namespace bedrockai {

struct LoginCredentialMaterial {
    // Exact Certificate JSON returned by the authentication service, usually {"chain":[...]}.
    std::string login_chain_json;
    // Modern multiplayer token. Empty only for a legacy-style credential flow.
    std::string multiplayer_token;
    // Raw ClientData JSON object to be signed by the same P-384 identity key.
    std::string client_data_json;
    // 0 = modern authenticated flow; 2 = offline/test identity flow.
    std::uint8_t authentication_type = 0;
};

struct LoginAuthRequestContext {
    ProtocolIdentity protocol{};
    std::string server_address;
    std::string client_public_key_base64;
};

class BedrockLoginAuthProvider {
public:
    virtual ~BedrockLoginAuthProvider() = default;
    virtual bool get_credentials(const LoginAuthRequestContext& context,
                                 LoginCredentialMaterial& out,
                                 std::string* error = nullptr) = 0;
};

// Host-owned callback provider. The callback is responsible for obtaining credentials
// from the real Minecraft/Microsoft authentication environment; this layer never mints tokens.
class CallbackLoginAuthProvider final : public BedrockLoginAuthProvider {
public:
    using Callback = std::function<bool(const LoginAuthRequestContext&,
                                        LoginCredentialMaterial&,
                                        std::string*)>;

    explicit CallbackLoginAuthProvider(Callback callback)
        : callback_(std::move(callback)) {}

    bool get_credentials(const LoginAuthRequestContext& context,
                         LoginCredentialMaterial& out,
                         std::string* error = nullptr) override;

private:
    Callback callback_;
};

// Host/application-owned provider. It accepts material obtained from a real auth service;
// it does not mint or fabricate Microsoft/Xbox credentials.
class StaticLoginAuthProvider final : public BedrockLoginAuthProvider {
public:
    explicit StaticLoginAuthProvider(LoginCredentialMaterial material)
        : material_(std::move(material)) {}

    bool get_credentials(const LoginAuthRequestContext& context,
                         LoginCredentialMaterial& out,
                         std::string* error = nullptr) override;

private:
    LoginCredentialMaterial material_;
};

struct LoginArtifact {
    std::vector<std::uint8_t> connection_request;
    std::vector<std::uint8_t> login_packet_payload;
    EcKeyPairDer identity_key;
    std::string client_public_key_base64;
};

// Optional provider path for a previously-created, target-bound Login payload.
// Used by a host-side login utility when it already signed ClientData with the
// same persistent P-384 key that must later complete Bedrock encryption.
class BedrockPrebuiltLoginProvider {
public:
    virtual ~BedrockPrebuiltLoginProvider() = default;
    virtual bool get_prebuilt_login(const LoginAuthRequestContext& context,
                                   LoginArtifact& out,
                                   std::string* error = nullptr) = 0;
};

class Bedrock2193LoginPayloadBuilder {
public:
    explicit Bedrock2193LoginPayloadBuilder(BedrockCryptoProvider& crypto)
        : crypto_(crypto) {}

    bool build(const LoginAuthRequestContext& context,
               BedrockLoginAuthProvider& auth,
               LoginArtifact& out,
               std::string* error = nullptr);

    static bool encode_login_packet_payload(std::int32_t protocol_version,
                                            const std::vector<std::uint8_t>& connection_request,
                                            std::vector<std::uint8_t>& out,
                                            std::string* error = nullptr);

private:
    BedrockCryptoProvider& crypto_;

    static bool extract_first_chain_token(const std::string& chain_json,
                                          std::string& token,
                                          std::string* error);
    static bool extract_json_string_field(const std::string& json,
                                          const std::string& field,
                                          std::string& value);
    static bool json_string_escape(const std::string& input, std::string& out);
    static std::vector<std::uint8_t> base64url_decode(const std::string& input, bool* ok);
    static std::string base64url_encode(const std::vector<std::uint8_t>& input);

    bool sign_es384_compact_jwt(const std::string& header_json,
                                const std::string& payload_json,
                                const EcKeyPairDer& key,
                                std::string& jwt,
                                std::string* error);
    bool validate_multiplayer_token_public_key(const LoginCredentialMaterial& material,
                                                const std::string& expected_public_key_b64,
                                                std::string* error);
};

} // namespace bedrockai
