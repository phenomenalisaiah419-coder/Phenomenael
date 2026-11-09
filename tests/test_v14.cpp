#include "core/login_payload.hpp"
#include "core/recording.hpp"
#include "core/secure_runtime.hpp"
#include "core/socket_datagram.hpp"
#include "interoperability/local_bedrock_server.hpp"
#include "native_controller.hpp"

#include <chrono>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace bedrockai;
using namespace std::chrono_literals;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++failures; } } while(false)

class CallbackAuth final : public BedrockLoginAuthProvider {
public:
    bool called = false;
    bool get_credentials(const LoginAuthRequestContext& context, LoginCredentialMaterial& out,
                         std::string* error) override {
        called = true;
        if (!context.protocol.is_target()) { if (error) *error = "bad protocol"; return false; }
        out.login_chain_json = "{\"chain\":[\"eyJhbGciOiJFUzM4NCIsIng1dSI6IkFBRkVYQU1QTEUifQ.eyJ4IjoxfQ.sig\"]}";
        out.client_data_json = "{\"GameVersion\":\"1.26.50\",\"ServerAddress\":\"127.0.0.1:19132\"}";
        out.authentication_type = 2;
        return true;
    }
};

static void test_callback_auth() {
    CallbackAuth auth;
    LoginAuthRequestContext ctx{{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet},
                                "127.0.0.1:19132", ""};
    LoginCredentialMaterial out;
    std::string error;
    CHECK(auth.get_credentials(ctx, out, &error), "callback auth provider invoked");
    CHECK(auth.called, "callback provider records invocation");
    CHECK(out.authentication_type == 2, "callback material preserved");
}

static void test_recorder_roundtrip() {
    TransportRecorder recorder(4);
    TransportFrame frame;
    frame.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    frame.direction = Direction::Clientbound;
    frame.packet_id = packet_id::StartGame;
    frame.sequence = 77;
    frame.payload = {1,2,3,4};
    std::string error;
    CHECK(recorder.record(frame, 12345, &error), "recorder accepts target frame");
    const std::string path = (std::filesystem::temp_directory_path() / "bedrockai_v14_replay.bin").string();
    CHECK(recorder.write_file(path, &error), "replay file writes");
    std::vector<RecordedFrame> loaded;
    CHECK(TransportRecorder::read_file(path, loaded, 4, &error), "replay file reads");
    CHECK(loaded.size() == 1, "replay frame count preserved");
    if (loaded.size() == 1) {
        CHECK(loaded[0].timestamp_ms == 12345, "replay timestamp preserved");
        CHECK(loaded[0].frame.packet_id == packet_id::StartGame, "replay packet id preserved");
        CHECK(loaded[0].frame.payload == frame.payload, "replay payload preserved");
        CHECK(loaded[0].frame.identity.transport == BedrockTransport::RakNet, "replay transport preserved");
    }
}

static void test_recorder_v1_compatibility() {
    const std::string path = (std::filesystem::temp_directory_path() / "bedrockai_v14_replay_v1.bin").string();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    const auto put16=[&](std::uint16_t v){ f.put(static_cast<char>(v)); f.put(static_cast<char>(v>>8)); };
    const auto put32=[&](std::uint32_t v){ for(int i=0;i<4;++i){ f.put(static_cast<char>(v)); v>>=8; } };
    const auto put64=[&](std::uint64_t v){ for(int i=0;i<8;++i){ f.put(static_cast<char>(v)); v>>=8; } };
    f.write("BAIRPL01", 8); put16(1); put32(1); put64(9); put16(1);
    f.put(static_cast<char>(Direction::Serverbound)); put32(packet_id::Login); put64(4); put32(2); f.put(char(0xaa)); f.put(char(0xbb)); f.close();
    std::vector<RecordedFrame> loaded; std::string error;
    CHECK(TransportRecorder::read_file(path, loaded, 4, &error), "legacy v1 replay reads");
    CHECK(loaded.size() == 1, "legacy v1 replay count preserved");
    if (loaded.size() == 1) {
        CHECK(loaded[0].frame.identity.transport == BedrockTransport::Unknown, "legacy replay transport defaults unknown");
        CHECK(loaded[0].frame.payload == std::vector<std::uint8_t>({0xaa,0xbb}), "legacy replay payload preserved");
    }
}

static void test_android_c_abi() {
    const std::uintptr_t handle = bedrockai_native_create();
    CHECK(handle != 0, "native controller handle created");
    CHECK(bedrockai_native_configure(handle,
                                     static_cast<int>(WorldTarget::LocalWorld),
                                     static_cast<int>(TransportPreference::RakNet), 1) == 1,
          "native controller configures embedded local-world mode");
    CHECK(bedrockai_native_local_control_capable(handle) == 1, "native local control capability exposed");
    CHECK(bedrockai_native_remote_control_capable(handle) == 1, "embedded controller can target remote servers too");
    bedrockai_native_destroy(handle);
}

static void test_local_interop() {
#if defined(_WIN32)
    std::cout << "local UDP interoperability harness skipped on Windows build\n";
    return;
#else
    interop::LocalBedrock2193Server server;
    std::string error;
    CHECK(server.start(&error), std::string("local Bedrock server starts: ") + error);
    if (!server.running()) return;

    raknet::SocketDatagramIo io(2048);
    raknet::RakNetConfig rcfg;
    rcfg.endpoint = {"127.0.0.1", server.port()};
    rcfg.mtu = 1492;
    rcfg.max_datagram_bytes = 2048;
    rcfg.identity.transport = BedrockTransport::RakNet;
    raknet::RakNetTransportBackend backend(rcfg);
    backend.set_io(&io);

    OpenSSLBedrockCryptoProvider crypto;
    SecureRuntimeConfig cfg;
    cfg.network.expected.transport = BedrockTransport::RakNet;
    cfg.bootstrap.expected.transport = BedrockTransport::RakNet;
    Bedrock2193SecureRuntime runtime(crypto, cfg);
    CHECK(runtime.attach(backend, &error), std::string("secure runtime attaches: ") + error);

    struct AuthProvider final : BedrockLoginAuthProvider {
        bool get_credentials(const LoginAuthRequestContext& context, LoginCredentialMaterial& out,
                             std::string*) override {
            if (!context.protocol.is_target() || context.client_public_key_base64.empty()) return false;
            out.login_chain_json =
                "{\"chain\":[\"eyJhbGciOiJFUzM4NCIsIng1dSI6IkFBRkVYQU1QTEUifQ.eyJ4IjoxfQ.sig\"]}";
            out.client_data_json =
                "{\"GameVersion\":\"1.26.50\",\"ServerAddress\":\"127.0.0.1:" +
                std::to_string(context.protocol.protocol + 1) + "\"}";
            out.authentication_type = 2;
            return true;
        }
    } auth;

    const LoginAuthRequestContext ctx{{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet},
                                      "127.0.0.1:" + std::to_string(server.port()), ""};
    CHECK(runtime.start_login(auth, ctx, &error), std::string("secure runtime starts login: ") + error);

    bool done = false;
    for (int i = 0; i < 1000 && !done; ++i) {
        std::string e1, e2, e3;
        const bool s_ok = server.poll(&e1);
        const bool c_ok = backend.poll(&e2);
        const bool r_ok = runtime.pump(&e3);
        if (!s_ok || !c_ok || !r_ok) {
            // Transient no-data conditions are normal. Any explicit protocol/transport error aborts.
            if ((!e1.empty() && e1 != "") || (!e2.empty() && e2 != "") || (!e3.empty() && e3 != "")) {
                // poll() returns true for no data; non-empty errors are meaningful.
                if (!s_ok) { std::cerr << "interop server: " << e1 << "\n"; }
                if (!c_ok) { std::cerr << "interop client: " << e2 << "\n"; }
                if (!r_ok) { std::cerr << "interop runtime: " << e3 << "\n"; }
                break;
            }
        }
        done = runtime.phase() == LoginPhase::WorldReady;
        std::this_thread::sleep_for(1ms);
    }

    CHECK(server.saw_request_network_settings(), "server observed RequestNetworkSettings over real UDP");
    CHECK(server.saw_login(), "server observed Login over real UDP");
    CHECK(server.sent_server_handshake(), "server sent verified ServerToClientHandshake");
    CHECK(server.saw_client_handshake(), "server decrypted ClientToServerHandshake");
    CHECK(server.sent_start_game(), "server sent encrypted StartGame");
    CHECK(runtime.encryption_enabled(), "client encryption enabled in real UDP path");
    CHECK(runtime.phase() == LoginPhase::WorldReady, "real UDP session reached WorldReady");
    CHECK(!server.recorder().frames().empty(), "interop harness recorded logical game frames");
    runtime.detach();
    server.stop();
#endif
}

int main() {
    test_callback_auth();
    test_recorder_roundtrip();
    test_recorder_v1_compatibility();
    test_android_c_abi();
    test_local_interop();
    if (failures) { std::cerr << "v14 tests: " << failures << " failure(s)\n"; return 1; }
    std::cout << "v14 tests: PASS\n";
    return 0;
}
