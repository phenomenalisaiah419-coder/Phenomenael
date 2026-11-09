#include "core/embedded_client_bridge.hpp"
#include "core/nethernet_transport.hpp"
#include "core/network_recovery.hpp"
#include "core/replay_backend.hpp"
#include "core/recording.hpp"
#include "nethernet_host.hpp"
#include "native_controller.hpp"

#include <iostream>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace bedrockai;
static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++failures; } } while(false)

static void test_recovery() {
    TransportRecoveryCoordinator recovery(2);
    int reconnects = 0;
    recovery.set_reconnect_handler([&](std::string*) { ++reconnects; return true; });
    recovery.on_network_lost();
    CHECK(recovery.recovery_required(), "network loss requests recovery");
    CHECK(recovery.generation() == 1, "network generation increments");
    std::string error;
    CHECK(recovery.on_network_available_with_hint(1, &error), "network available triggers reconnect");
    CHECK(reconnects == 1, "exactly one reconnect attempt after loss");
    CHECK(recovery.generation() == 1, "successful reconnect preserves network generation");
    CHECK(!recovery.recovery_required(), "successful reconnect clears recovery");
    CHECK(!recovery.on_network_available_with_hint(0, &error), "stale generation is rejected");
}

static void test_replay_backend() {
    TransportRecorder recorder(8);
    TransportFrame frame;
    frame.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::NetherNet};
    frame.direction = Direction::Clientbound;
    frame.packet_id = packet_id::StartGame;
    frame.sequence = 5;
    std::string error;
    CHECK(recorder.record(frame, 1, &error), "record start game");
    ReplayBackendConfig config;
    config.identity.transport = BedrockTransport::NetherNet;
    ReplayTransportBackend replay(config);
    CHECK(replay.load(recorder.frames(), &error), "load replay");
    int received = 0;
    replay.set_receive_handler([&](const TransportFrame&) { ++received; });
    CHECK(replay.connect(config.identity, &error), "connect replay backend");
    CHECK(replay.step(&error), "replay one frame");
    CHECK(received == 1, "replay delivers clientbound frame");
}

static void test_nethernet_backend() {
    NetherNetDataChannelIo io;
    std::vector<std::uint8_t> outbound;
    io.set_send_handler([&](const std::vector<std::uint8_t>& bytes, std::string*) { outbound = bytes; return true; });
    NetherNetTransportBackend backend(io);
    ProtocolIdentity identity{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::NetherNet};
    std::string error;
    CHECK(backend.connect(identity, &error), "NetherNet backend connects host data-channel seam");
    TransportFrame frame;
    frame.identity = identity;
    frame.direction = Direction::Serverbound;
    frame.packet_id = 0xfe;
    frame.payload = {0xfe, 0x01, 0x02};
    CHECK(backend.send(frame, &error), "NetherNet backend sends raw Bedrock batch");
    CHECK(outbound == frame.payload, "NetherNet backend sends exact Bedrock batch bytes");
    io.receive(outbound);
    int received = 0;
    backend.set_receive_handler([&](const TransportFrame& got) { ++received; CHECK(got.packet_id == 0xfe, "NetherNet packet id is Bedrock batch"); CHECK(got.payload == frame.payload, "NetherNet batch bytes preserved"); });
    CHECK(backend.poll(&error), "NetherNet backend polls data channel");
    CHECK(received == 1, "NetherNet backend delivered decoded batch frame");
    frame.packet_id = packet_id::ClientToServerHandshake;
    CHECK(!backend.send(frame, &error), "NetherNet rejects non-batch transport frame");
}


static void test_replay_file_ingestion() {
    TransportRecorder recorder(8);
    TransportFrame frame;
    frame.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    frame.direction = Direction::Clientbound;
    frame.packet_id = 0xfe;
    frame.sequence = 11;
    frame.payload = {0xfe, 0x00};
    std::string error;
    CHECK(recorder.record(frame, 99, &error), "record replay file frame");
    const std::string path = (std::filesystem::temp_directory_path() / "bedrockai_v15_replay.bin").string();
    CHECK(recorder.write_file(path, &error), "write replay file for backend ingestion");
    ReplayBackendConfig config;
    config.identity.transport = BedrockTransport::RakNet;
    ReplayTransportBackend replay(config);
    CHECK(replay.load_file(path, &error), "load replay file directly");
    int received = 0;
    replay.set_receive_handler([&](const TransportFrame& got) { ++received; CHECK(got.packet_id == 0xfe, "replay file packet id preserved"); });
    CHECK(replay.connect(config.identity, &error), "replay file backend connects");
    CHECK(replay.step(&error), "replay file backend steps");
    CHECK(received == 1, "replay file backend delivers frame");
}

static void test_android_lifecycle_boundary() {
    bedrockai::android_host::NativeControllerHandle handle;
    std::string error;
    CHECK(handle.configure(WorldTarget::RemoteServer, TransportPreference::RakNet, true, &error), "Android native lifecycle configures");
    handle.network_lost();
    CHECK(handle.reconnect_required(), "Android network loss marks reconnect required");
    CHECK(handle.network_generation() == 1, "Android network loss increments generation");
    CHECK(!handle.network_available(1, &error), "Android recovery fails safely without stored endpoint");
}

static void test_nethernet_concurrent_callbacks() {
    NetherNetDataChannelIo io;
    std::atomic<int> sent{0};
    io.set_send_handler([&](const std::vector<std::uint8_t>&, std::string*) { sent.fetch_add(1); return true; });
    CHECK(io.open(), "open concurrent NetherNet channel");
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::thread producer([&] { for (int i = 0; i < 1000; ++i) { io.receive({0xfe, static_cast<std::uint8_t>(i & 0xff)}); produced.fetch_add(1); } });
    std::thread consumer([&] {
        while (consumed.load() < 1000) {
            if (io.poll()) consumed.fetch_add(1);
            else std::this_thread::yield();
        }
    });
    producer.join();
    consumer.join();
    CHECK(sent.load() == 0 && produced.load() == 1000 && consumed.load() == 1000 && io.pending() == 0,
          "receive path is concurrency-safe and lossless");
}

static void test_android_host_nethernet() {
    const auto handle = bedrockai_nethernet_create();
    CHECK(handle != 0, "Android NetherNet host handle created");
    CHECK(bedrockai_nethernet_connect(handle) == 1, "Android NetherNet host connects");
    const std::uint8_t bytes[] = {0x01, 0x02};
    CHECK(bedrockai_nethernet_next_outbound_size(handle) == 0, "Android NetherNet host starts with empty outbound queue");
    CHECK(bedrockai_nethernet_receive(handle, bytes, sizeof(bytes)) == 1, "Android NetherNet host accepts channel data");
    CHECK(bedrockai_nethernet_poll(handle) == 0, "malformed NetherNet channel data is rejected");
    bedrockai_nethernet_destroy(handle);
}

static void test_embedded_bridge() {
    const auto handle = bedrockai_embedded_create();
    CHECK(handle != 0, "embedded client bridge created");
    BedrockAiEmbeddedTouch touch{};
    BedrockAiEmbeddedAi ai{};
    BedrockAiEmbeddedActions out{};
    ai.movement_valid = 1;
    ai.movement_forward = 1.0;
    ai.aim_valid = 1;
    ai.aim_yaw_delta = 2.0;
    CHECK(bedrockai_embedded_tick(handle, 3, 20.0, 1, 0, &touch, &ai, &out) == 1, "embedded tick accepted");
    CHECK(out.movement == 1 && out.forward == 1.0, "embedded bridge emits movement");
    CHECK(out.attack == 0, "embedded bridge does not invent human attack input");
    touch.attack = 1;
    ai.propose_attack = 1;
    CHECK(bedrockai_embedded_tick(handle, 4, 20.0, 1, 0, &touch, &ai, &out) == 1, "embedded attack tick accepted");
    CHECK(out.attack == 1, "human attack gate permits AI attack assistance");
    bedrockai_embedded_destroy(handle);
}

static void test_replay_serverbound_matching() {
    TransportRecorder recorder(8);
    TransportFrame expected{};
    expected.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    expected.direction = Direction::Serverbound;
    expected.packet_id = 0xfe;
    expected.payload = {0xfe, 0x10};
    std::string error;
    CHECK(recorder.record(expected, 1, &error), "record expected serverbound frame");
    ReplayBackendConfig config;
    config.identity.transport = BedrockTransport::RakNet;
    config.require_serverbound_match = true;
    ReplayTransportBackend replay(config);
    CHECK(replay.load(recorder.frames(), &error), "load serverbound matching replay");
    CHECK(replay.connect(config.identity, &error), "connect serverbound matching replay");
    CHECK(replay.send(expected, &error), "matching serverbound frame accepted");
    TransportFrame wrong = expected;
    wrong.payload = {0xfe, 0x11};
    CHECK(!replay.send(wrong, &error), "mismatched serverbound frame rejected");
}

static void test_deployment_input_validation() {
    DeploymentRequest unknown_target{};
    unknown_target.target = WorldTarget::Unknown;
    const auto target_decision = DeploymentPlanner::plan(unknown_target);
    CHECK(!target_decision.accepted, "unknown world target is rejected");

    DeploymentRequest invalid_mode{};
    invalid_mode.target = WorldTarget::RemoteServer;
    invalid_mode.mode = static_cast<DeploymentMode>(255);
    const auto mode_decision = DeploymentPlanner::plan(invalid_mode);
    CHECK(!mode_decision.accepted, "invalid deployment mode is rejected");

    DeploymentRequest invalid_transport{};
    invalid_transport.target = WorldTarget::RemoteServer;
    invalid_transport.transport = static_cast<TransportPreference>(255);
    const auto transport_decision = DeploymentPlanner::plan(invalid_transport);
    CHECK(!transport_decision.accepted, "invalid transport preference is rejected");
}

int main() {
    test_recovery();
    test_replay_backend();
    test_replay_file_ingestion();
    test_replay_serverbound_matching();
    test_nethernet_backend();
    test_android_host_nethernet();
    test_android_lifecycle_boundary();
    test_nethernet_concurrent_callbacks();
    test_embedded_bridge();
    test_deployment_input_validation();
    if (failures != 0) { std::cerr << "v15 tests: " << failures << " failure(s)\n"; return 1; }
    std::cout << "v15 tests: PASS\n";
    return 0;
}
