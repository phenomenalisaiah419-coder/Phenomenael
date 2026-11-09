#include "core/auth_cache.hpp"
#include "core/secure_runtime.hpp"
#include "core/game_batch.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace bedrockai;
static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { ++failures; std::cerr << "FAIL: " << (msg) << "\n"; } } while (false)

static std::string b64std(const std::vector<std::uint8_t>& input) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    std::uint32_t acc = 0U;
    unsigned bits = 0U;
    for (const auto byte : input) {
        acc = (acc << 8U) | byte;
        bits += 8U;
        while (bits >= 6U) { bits -= 6U; output.push_back(table[(acc >> bits) & 0x3fU]); }
    }
    if (bits != 0U) output.push_back(table[(acc << (6U - bits)) & 0x3fU]);
    while (output.size() % 4U != 0U) output.push_back('=');
    return output;
}

static std::string pem_from_der(const std::vector<std::uint8_t>& der) {
    const std::string encoded = b64std(der);
    std::string pem = "-----BEGIN PRIVATE KEY-----\n";
    for (std::size_t i = 0U; i < encoded.size(); i += 64U) pem += encoded.substr(i, 64U) + "\n";
    pem += "-----END PRIVATE KEY-----\n";
    return pem;
}

static std::vector<std::uint8_t> login_payload() {
    // protocol 2193 + VarUInt32 length=3 + a deliberately opaque test ConnectionRequest.
    return {0x00U, 0x00U, 0x08U, 0x91U, 0x03U, 0xaaU, 0xbbU, 0xccU};
}

static void put(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
static void put_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}


class CacheRuntimeBackend final : public BedrockTransportBackend {
public:
    bool connected = false;
    std::vector<TransportFrame> sent;
    ReceiveHandler handler;

    bool connect(const ProtocolIdentity& identity, std::string* error) override {
        if (!identity.is_target()) { if (error) *error = "invalid protocol"; return false; }
        connected = true;
        return true;
    }
    bool send(const TransportFrame& frame, std::string* error) override {
        if (!connected) { if (error) *error = "not connected"; return false; }
        sent.push_back(frame);
        return true;
    }
    void close() override { connected = false; }
    TransportState state() const override { return connected ? TransportState::Authenticating : TransportState::Disconnected; }
    void set_receive_handler(ReceiveHandler value) override { handler = std::move(value); }
};

static TransportFrame network_settings_frame() {
    TransportFrame frame;
    frame.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    frame.direction = Direction::Clientbound;
    frame.packet_id = packet_id::NetworkSettings;
    frame.sequence = 7U;
    frame.payload = {0x00U, 0x02U, 0xffU, 0xffU, 0x00U, 0x00U, 0x00U, 0x00U, 0x80U, 0x3fU};
    return frame;
}

static void test_secure_runtime_uses_prebuilt_cache(FileLoginCacheProvider& provider,
                                                   const LoginAuthRequestContext& context,
                                                   const std::vector<std::uint8_t>& expected_payload) {
    OpenSSLBedrockCryptoProvider crypto;
    SecureRuntimeConfig config;
    config.network.expected.transport = BedrockTransport::RakNet;
    config.bootstrap.expected.transport = BedrockTransport::RakNet;
    Bedrock2193SecureRuntime runtime(crypto, config);
    CacheRuntimeBackend backend;
    std::string error;
    CHECK(runtime.attach(backend, &error), "secure runtime attaches to cache test backend");
    CHECK(runtime.start_login(provider, context, &error), "secure runtime starts with prebuilt cache provider");
    CHECK(backend.sent.size() == 1U, "secure runtime first sends RequestNetworkSettings");
    if (!backend.sent.empty()) {
        CHECK(backend.sent[0].packet_id == 0xfeU, "RequestNetworkSettings is wrapped in a Bedrock batch");
        wire::BatchCodecConfig batch_config;
        auto request = wire::BedrockBatchCodec::decode(backend.sent[0].payload, batch_config, nullptr);
        CHECK(request.has_value() && request->size() == 1U && request->at(0).packet_id == packet_id::RequestNetworkSettings,
              "first batch contains RequestNetworkSettings");
    }
    CHECK(runtime.handle_frame(network_settings_frame(), &error), "NetworkSettings triggers prebuilt cache path");
    CHECK(backend.sent.size() == 2U, "secure runtime sends Login after NetworkSettings");
    if (backend.sent.size() >= 2U) {
        wire::BatchCodecConfig batch_config;
        auto login = wire::BedrockBatchCodec::decode(backend.sent[1].payload, batch_config, nullptr);
        CHECK(login.has_value() && login->size() == 1U && login->at(0).packet_id == packet_id::Login,
              "second batch contains Login");
        if (login && login->size() == 1U) {
            CHECK(login->at(0).payload == expected_payload, "runtime transmits cache Login payload byte-for-byte");
        }
    }
    CHECK(!runtime.login_artifact().identity_key.private_key_pkcs8_der.empty(),
          "runtime retains the matching P-384 identity key for encryption handshake");
    runtime.detach();
}

int main() {
    const auto temp = std::filesystem::temp_directory_path() / "bedrock_ai_v26_cache_test";
    std::error_code ec;
    std::filesystem::remove_all(temp, ec);
    std::filesystem::create_directories(temp);

    OpenSSLBedrockCryptoProvider crypto;
    EcKeyPairDer key;
    std::string error;
    CHECK(crypto.generate_p384_keypair(key, &error), "generates test P-384 key");
    put_text(temp / "ecdsa_p384.pem", pem_from_der(key.private_key_pkcs8_der));
    put(temp / "login_payload.bin", login_payload());
    const std::string profile = std::string("{\n") +
        "  \"display_name\": \"TestUser\",\n" +
        "  \"game_version\": \"1.26.50\",\n" +
        "  \"protocol\": 2193,\n" +
        "  \"server_address\": \"example.org:19132\",\n" +
        "  \"identity_public_key_spki_b64\": \"" + b64std(key.public_key_pkix_der) + "\"\n" +
        "}\n";
    put_text(temp / "profile.json", profile);

    FileLoginCacheProvider provider(crypto, temp.string());
    LoginAuthRequestContext context;
    context.protocol = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    context.server_address = "example.org:19132";
    LoginArtifact artifact;
    CHECK(provider.get_prebuilt_login(context, artifact, &error), "loads target-bound cache");
    CHECK(artifact.login_packet_payload == login_payload(), "prebuilt Login payload is preserved byte-for-byte");
    CHECK(artifact.identity_key.private_key_pkcs8_der == key.private_key_pkcs8_der, "P-384 PKCS#8 key is loaded");
    CHECK(artifact.identity_key.public_key_pkix_der == key.public_key_pkix_der, "cached public key is loaded");
    test_secure_runtime_uses_prebuilt_cache(provider, context, login_payload());

    context.server_address = "other.example:19132";
    CHECK(!provider.get_prebuilt_login(context, artifact, &error), "rejects a cache signed for a different server");
    CHECK(error.find("signed for example.org:19132") != std::string::npos, "server mismatch is actionable");

    context.server_address = "example.org:19132";
    auto corrupt_payload = login_payload();
    corrupt_payload[3] = 0x90U;
    put(temp / "login_payload.bin", corrupt_payload);
    CHECK(!provider.get_prebuilt_login(context, artifact, &error), "rejects mismatched Bedrock protocol header");
    CHECK(error.find("protocol") != std::string::npos, "protocol mismatch is diagnosed");

    put(temp / "login_payload.bin", login_payload());
    put_text(temp / "ecdsa_p384.pem", "not a private key");
    CHECK(!provider.get_prebuilt_login(context, artifact, &error), "rejects malformed PEM before handshake");
    CHECK(error.find("PKCS#8") != std::string::npos, "malformed key has explicit diagnostic");

    LoginCredentialMaterial material;
    CHECK(!provider.get_credentials(context, material, &error), "prebuilt provider refuses accidental rebuild path");
    CHECK(error.find("prebuilt Login artifact") != std::string::npos, "wrong provider method is diagnosed");

    std::filesystem::remove_all(temp, ec);
    if (failures != 0) {
        std::cerr << failures << " v26 checks failed\n";
        return 1;
    }
    std::cout << "v26 auth-cache tests passed\n";
    return 0;
}
