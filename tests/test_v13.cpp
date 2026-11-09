#include "core/bedrock_handshake.hpp"
#include "core/secure_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace bedrockai;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++failures; } } while(false)

static std::string b64url(const std::vector<std::uint8_t>& input) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    std::uint32_t acc = 0; unsigned bits = 0;
    for (auto b : input) { acc = (acc << 8) | b; bits += 8; while (bits >= 6) { bits -= 6; out.push_back(table[(acc >> bits) & 63]); } }
    if (bits) out.push_back(table[(acc << (6 - bits)) & 63]);
    return out;
}

static std::string b64std(const std::vector<std::uint8_t>& input) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::uint32_t acc = 0; unsigned bits = 0;
    for (auto b : input) { acc = (acc << 8) | b; bits += 8; while (bits >= 6) { bits -= 6; out.push_back(table[(acc >> bits) & 63]); } }
    if (bits) out.push_back(table[(acc << (6 - bits)) & 63]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

static std::string sign_jwt(OpenSSLBedrockCryptoProvider& crypto,
                            const EcKeyPairDer& signer_key,
                            const std::string& header_json,
                            const std::string& payload_json) {
    const std::string h = b64url(std::vector<std::uint8_t>(header_json.begin(), header_json.end()));
    const std::string p = b64url(std::vector<std::uint8_t>(payload_json.begin(), payload_json.end()));
    const std::string input = h + "." + p;
    std::vector<std::uint8_t> sig;
    std::string error;
    if (!crypto.sign_es384(signer_key.private_key_pkcs8_der,
                           std::vector<std::uint8_t>(input.begin(), input.end()), sig, &error)) {
        std::cerr << "sign fixture failed: " << error << "\n";
        ++failures;
        return {};
    }
    return input + "." + b64url(sig);
}

static std::vector<std::uint8_t> make_settings(std::uint16_t threshold, std::uint16_t algorithm) {
    std::vector<std::uint8_t> p{
        static_cast<std::uint8_t>(threshold & 0xff), static_cast<std::uint8_t>(threshold >> 8),
        static_cast<std::uint8_t>(algorithm & 0xff), static_cast<std::uint8_t>(algorithm >> 8),
        0, 0
    };
    const std::uint32_t one = 0x3f800000u;
    p.push_back(static_cast<std::uint8_t>(one & 0xff));
    p.push_back(static_cast<std::uint8_t>((one >> 8) & 0xff));
    p.push_back(static_cast<std::uint8_t>((one >> 16) & 0xff));
    p.push_back(static_cast<std::uint8_t>((one >> 24) & 0xff));
    return p;
}

class ConnectingBackend final : public BedrockTransportBackend {
public:
    bool connected = false;
    bool ready = false;
    std::vector<TransportFrame> sent;
    ReceiveHandler handler;

    bool connect(const ProtocolIdentity& identity, std::string* error) override {
        if (!identity.is_target()) { if (error) *error = "bad identity"; return false; }
        connected = true; return true;
    }
    bool send(const TransportFrame& frame, std::string* error) override {
        if (!ready) { if (error) *error = "still connecting"; return false; }
        sent.push_back(frame); return true;
    }
    void close() override { connected = false; ready = false; }
    TransportState state() const override {
        if (!connected) return TransportState::Disconnected;
        return ready ? TransportState::Authenticating : TransportState::Connecting;
    }
    void set_receive_handler(ReceiveHandler h) override { handler = std::move(h); }
};

class FakeBackend final : public BedrockTransportBackend {
public:
    bool connected = false;
    std::vector<TransportFrame> sent;
    ReceiveHandler handler;

    bool connect(const ProtocolIdentity& identity, std::string* error) override {
        if (!identity.is_target()) { if (error) *error = "bad identity"; return false; }
        connected = true; return true;
    }
    bool send(const TransportFrame& frame, std::string* error) override {
        if (!connected) { if (error) *error = "not connected"; return false; }
        sent.push_back(frame); return true;
    }
    void close() override { connected = false; }
    TransportState state() const override { return connected ? TransportState::InGame : TransportState::Disconnected; }
    void set_receive_handler(ReceiveHandler h) override { handler = std::move(h); }
    void push(TransportFrame frame) { if (handler) handler(frame); }
};

class ContextProvider final : public BedrockLoginAuthProvider {
public:
    explicit ContextProvider(LoginCredentialMaterial material) : material_(std::move(material)) {}
    LoginCredentialMaterial material_;
    bool get_credentials(const LoginAuthRequestContext& ctx, LoginCredentialMaterial& out, std::string*) override {
        out = material_;
        const std::string token_header = b64url(std::vector<std::uint8_t>{'{','"','a','l','g','"',':','"','n','o','n','e','"','}'});
        const std::string payload = std::string("{\"cpk\":\"") + ctx.client_public_key_base64 + "\"}";
        const std::string token_payload = b64url(std::vector<std::uint8_t>(payload.begin(), payload.end()));
        out.multiplayer_token = token_header + "." + token_payload + ".test";
        return true;
    }
};

static TransportFrame frame(std::uint32_t packet_id, std::vector<std::uint8_t> payload) {
    TransportFrame f;
    f.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    f.direction = Direction::Clientbound;
    f.packet_id = packet_id;
    f.sequence = 1;
    f.payload = std::move(payload);
    return f;
}

static void test_zero_payload_batch() {
    wire::BatchCodecConfig cfg;
    auto encoded = wire::BedrockBatchCodec::encode({std::vector<std::uint8_t>{static_cast<std::uint8_t>(packet_id::ClientToServerHandshake)}}, cfg, nullptr);
    CHECK(encoded.has_value(), "zero-payload packet encodes");
    if (!encoded) return;
    auto decoded = wire::BedrockBatchCodec::decode(*encoded, cfg, nullptr);
    CHECK(decoded.has_value() && decoded->size() == 1, "zero-payload packet decodes");
    if (decoded && decoded->size() == 1) {
        CHECK(decoded->at(0).packet_id == packet_id::ClientToServerHandshake, "zero-payload packet id preserved");
        CHECK(decoded->at(0).payload.empty(), "zero-payload packet remains empty");
    }
}

static void test_transport_connecting_gate() {
    ConnectingBackend backend;
    Protocol2193Session session;
    std::string e;
    CHECK(session.attach(backend, &e), "session attaches while backend is connecting");
    CHECK(session.state() == TransportState::Connecting, "session preserves backend Connecting state");
    CHECK(session.queue_serverbound(packet_id::Login, {1,2,3}, &e),
          "serverbound payload can be staged before transport connection completes");
    CHECK(!session.flush(&e), "flush is blocked until backend transport is connected");
    backend.ready = true;
    CHECK(session.flush(&e), "staged payload flushes after transport connects");
    CHECK(session.pending_outbound() == 0, "staged payload leaves queue after successful send");
}

static void test_handshake_processor() {
    OpenSSLBedrockCryptoProvider crypto;
    std::string e;
    EcKeyPairDer client, server;
    CHECK(crypto.generate_p384_keypair(client, &e), "client key generated");
    CHECK(crypto.generate_p384_keypair(server, &e), "server key generated");
    const std::vector<std::uint8_t> salt{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    const std::string header = std::string("{\"alg\":\"ES384\",\"x5u\":\"") + b64std(server.public_key_pkix_der) + "\"}";
    const std::string payload = std::string("{\"salt\":\"") + b64std(salt) + "\"}";
    const std::string jwt = sign_jwt(crypto, server, header, payload);

    BedrockServerToClientHandshakeProcessor processor(crypto);
    ServerEncryptionHandshake result;
    CHECK(processor.process(std::vector<std::uint8_t>(jwt.begin(), jwt.end()), client.private_key_pkcs8_der, result, &e),
          std::string("server handshake verifies: ") + e);
    CHECK(result.salt == salt, "salt decoded exactly");
    CHECK(result.server_public_key_pkix_der == server.public_key_pkix_der, "server public key preserved");

    std::vector<std::uint8_t> client_shared, server_shared;
    CHECK(crypto.p384_shared_secret_x(client.private_key_pkcs8_der, server.public_key_pkix_der, client_shared, &e), "client ECDH");
    CHECK(crypto.p384_shared_secret_x(server.private_key_pkcs8_der, client.public_key_pkix_der, server_shared, &e), "server ECDH");
    CHECK(client_shared == server_shared && client_shared.size() == 48, "ECDH X-coordinate agrees");
    std::vector<std::uint8_t> material = salt;
    material.insert(material.end(), client_shared.begin(), client_shared.end());
    std::array<std::uint8_t,32> expected{};
    CHECK(crypto.sha256(material, expected, &e), "SHA-256 key derivation");
    CHECK(result.encryption_key == expected, "encryption key equals SHA-256(salt||sharedX)");

    auto tampered = std::vector<std::uint8_t>(jwt.begin(), jwt.end());
    tampered[tampered.size()-2] = tampered[tampered.size()-2] == 'A' ? 'B' : 'A';
    CHECK(!processor.process(tampered, client.private_key_pkcs8_der, result, &e), "tampered handshake rejected");

    const std::string bad_alg = sign_jwt(crypto, server, "{\"alg\":\"HS256\",\"x5u\":\"" + b64std(server.public_key_pkix_der) + "\"}", payload);
    CHECK(!processor.process(std::vector<std::uint8_t>(bad_alg.begin(), bad_alg.end()), client.private_key_pkcs8_der, result, &e), "wrong JWT algorithm rejected");
}

static void test_secure_runtime() {
    OpenSSLBedrockCryptoProvider crypto;
    FakeBackend backend;
    SecureRuntimeConfig cfg;
    cfg.network.expected.transport = BedrockTransport::RakNet;
    cfg.bootstrap.expected.transport = BedrockTransport::RakNet;
    Bedrock2193SecureRuntime runtime(crypto, cfg);
    std::string e;
    int forwarded_start_game_count = 0;
    runtime.set_receive_handler([&forwarded_start_game_count](const TransportFrame& received) {
        if (received.direction == Direction::Clientbound && received.packet_id == packet_id::StartGame) {
            ++forwarded_start_game_count;
        }
    });
    CHECK(runtime.attach(backend, &e), std::string("runtime attached: ") + e);

    OpenSSLBedrockCryptoProvider fixture_crypto;
    EcKeyPairDer chain_key;
    CHECK(fixture_crypto.generate_p384_keypair(chain_key, &e), "chain fixture key generated");
    const std::string first_chain_header = std::string("{\"x5u\":\"") + b64std(chain_key.public_key_pkix_der) + "\"}";
    const std::string first_chain = b64url(std::vector<std::uint8_t>(first_chain_header.begin(), first_chain_header.end())) + ".p.s";
    LoginCredentialMaterial material;
    material.authentication_type = 0;
    material.login_chain_json = std::string("{\"chain\":[\"") + first_chain + "\"]}";
    material.client_data_json = "{\"GameVersion\":\"1.26.50\",\"ServerAddress\":\"127.0.0.1:19132\"}";
    ContextProvider provider(material);
    LoginAuthRequestContext ctx{{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet}, "127.0.0.1:19132", ""};
    CHECK(runtime.start_login(provider, ctx, &e), std::string("runtime login starts: ") + e);
    CHECK(backend.sent.size() == 1 && backend.sent[0].packet_id == 0xfe,
          "RequestNetworkSettings sent inside a Bedrock batch");
    wire::BatchCodecConfig initial_cfg;
    auto initial_decoded = wire::BedrockBatchCodec::decode(backend.sent[0].payload, initial_cfg, nullptr);
    CHECK(initial_decoded.has_value() && initial_decoded->size() == 1 && initial_decoded->at(0).packet_id == packet_id::RequestNetworkSettings,
          "initial batch contains RequestNetworkSettings");

    backend.push(frame(packet_id::NetworkSettings, make_settings(512, 0xffff)));
    CHECK(backend.sent.size() == 2 && backend.sent[1].packet_id == 0xfe, "Login sent in a Bedrock batch after NetworkSettings");
    auto login_decoded = wire::BedrockBatchCodec::decode(backend.sent[1].payload, initial_cfg, nullptr);
    CHECK(login_decoded.has_value() && login_decoded->size() == 1 && login_decoded->at(0).packet_id == packet_id::Login,
          "login batch contains Login packet");
    CHECK(!runtime.login_artifact().identity_key.private_key_pkcs8_der.empty(), "runtime retains client key");

    EcKeyPairDer server_key;
    CHECK(crypto.generate_p384_keypair(server_key, &e), "server encryption key generated");
    const std::vector<std::uint8_t> salt{15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};
    const std::string server_header = std::string("{\"alg\":\"ES384\",\"x5u\":\"") + b64std(server_key.public_key_pkix_der) + "\"}";
    const std::string server_payload = std::string("{\"salt\":\"") + b64std(salt) + "\"}";
    const auto server_jwt = sign_jwt(crypto, server_key, server_header, server_payload);
    backend.push(frame(packet_id::ServerToClientHandshake, std::vector<std::uint8_t>(server_jwt.begin(), server_jwt.end())));
    CHECK(runtime.encryption_enabled(), "encryption enabled after verified server handshake");
    CHECK(runtime.phase() == LoginPhase::Authenticated, "runtime authenticated after encrypted client handshake");
    CHECK(backend.sent.size() == 3 && backend.sent[2].packet_id == 0xfe, "encrypted ClientToServerHandshake sent as batch");

    BedrockPacketCipher server_cipher;
    CHECK(server_cipher.enable(runtime.server_handshake().encryption_key, &e), "server cipher fixture enabled");
    auto encrypted = backend.sent[2].payload;
    CHECK(server_cipher.decrypt_packet(encrypted, &e), "server decrypts client handshake batch");
    wire::BatchCodecConfig batch_cfg;
    auto decoded = wire::BedrockBatchCodec::decode(encrypted, batch_cfg, nullptr);
    CHECK(decoded.has_value() && decoded->size() == 1 && decoded->at(0).packet_id == packet_id::ClientToServerHandshake,
          "decrypted client handshake is packet 4");

    std::vector<std::uint8_t> server_batch;
    wire::BatchCodecConfig server_batch_cfg;
    auto encoded = wire::BedrockBatchCodec::encode({std::vector<std::uint8_t>{static_cast<std::uint8_t>(packet_id::StartGame), 0x01}}, server_batch_cfg, nullptr);
    CHECK(encoded.has_value(), "server StartGame batch encoded");
    if (encoded) {
        CHECK(server_cipher.encrypt_packet(*encoded, &e), "server encrypts StartGame batch");
        auto before = runtime.phase();
        backend.push(frame(0xfe, *encoded));
        CHECK(before == LoginPhase::Authenticated, "runtime was authenticated before StartGame");
        CHECK(runtime.phase() == LoginPhase::WorldReady, "StartGame advances runtime to WorldReady");
        CHECK(runtime.network().state() == TransportState::InGame, "decoded StartGame advances session state");
        CHECK(forwarded_start_game_count == 1, "StartGame is also forwarded to gameplay observation subscribers");
    }
}

static void test_encrypted_compressed_batch() {
    OpenSSLBedrockCryptoProvider crypto;
    std::string e;
    EcKeyPairDer key;
    CHECK(crypto.generate_p384_keypair(key, &e), "compressed-batch key generated");
    BedrockPacketCipher sender_cipher, receiver_cipher;
    std::array<std::uint8_t,32> key_bytes{};
    for (std::size_t i = 0; i < key_bytes.size(); ++i) key_bytes[i] = static_cast<std::uint8_t>(0xa0u + i);
    CHECK(sender_cipher.enable(key_bytes, &e), "compressed sender cipher enabled");
    CHECK(receiver_cipher.enable(key_bytes, &e), "compressed receiver cipher enabled");

    wire::BatchCodecConfig cfg;
    cfg.compression_enabled = true;
    cfg.compression = wire::CompressionAlgorithm::Flate;
    cfg.compression_threshold = 1;
    const std::vector<std::uint8_t> payload{
        static_cast<std::uint8_t>(packet_id::StartGame), 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    };
    auto batch = wire::BedrockBatchCodec::encode({payload}, cfg, nullptr);
    CHECK(batch.has_value(), "compressed batch encodes");
    if (!batch) return;
    CHECK((*batch)[0] == 0xfe && (*batch)[1] == static_cast<std::uint8_t>(wire::CompressionAlgorithm::Flate),
          "batch carries negotiated compression marker");
    CHECK(sender_cipher.encrypt_packet(*batch, &e), "compressed batch encrypts");
    CHECK(receiver_cipher.decrypt_packet(*batch, &e), "compressed batch decrypts");
    auto decoded = wire::BedrockBatchCodec::decode(*batch, cfg, nullptr);
    CHECK(decoded.has_value() && decoded->size() == 1, "compressed encrypted batch decodes");
    if (decoded && decoded->size() == 1) {
        CHECK(decoded->at(0).packet_id == packet_id::StartGame, "compressed encrypted batch packet id preserved");
        CHECK(decoded->at(0).payload == std::vector<std::uint8_t>(payload.begin()+1, payload.end()),
              "compressed encrypted batch payload preserved");
    }
}

int main() {
    test_zero_payload_batch();
    test_transport_connecting_gate();
    test_encrypted_compressed_batch();
    test_handshake_processor();
    test_secure_runtime();
    if (failures) { std::cerr << "v13 tests: " << failures << " failure(s)\n"; return 1; }
    std::cout << "v13 tests: PASS\n";
    return 0;
}
