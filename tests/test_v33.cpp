#include "core/wire/movement_action_sender.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace bedrockai;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAIL: " << #condition << " at line " << __LINE__ << "\n"; \
    std::exit(1); } } while (false)

class MovementBackend final : public BedrockTransportBackend {
public:
    bool connect(const ProtocolIdentity& identity, std::string*) override {
        connected = identity.is_target();
        return connected;
    }
    bool send(const TransportFrame& frame, std::string*) override {
        if (!connected) return false;
        sent.push_back(frame);
        return true;
    }
    void close() override { connected = false; }
    TransportState state() const override { return connected ? TransportState::InGame : TransportState::Disconnected; }
    void set_receive_handler(ReceiveHandler handler) override { handler_ = std::move(handler); }
    bool connected = false;
    std::vector<TransportFrame> sent;
private:
    ReceiveHandler handler_;
};

static void enter_game(Protocol2193NetworkEngine& engine) {
    TransportFrame start;
    start.identity = {"1.26.50", 2193, BedrockTransport::RakNet};
    start.direction = Direction::Clientbound;
    start.packet_id = packet_id::StartGame;
    start.sequence = 1U;
    std::string error;
    CHECK(engine.session().ingest(start, &error));
    CHECK(engine.state() == TransportState::InGame);
}

static void test_movement_sends_exactly_once_until_server_tick_feedback() {
    NetworkEngineConfig config;
    config.batch.compression_enabled = false;
    Protocol2193NetworkEngine engine(config);
    MovementBackend backend;
    std::string error;
    CHECK(engine.attach(backend, &error));
    enter_game(engine);

    wire::MovementActionSender sender;
    wire::PlayerAuthInputPacket action;
    action.tick = 10U;
    action.position = {1.0, 64.0, -2.0};
    action.yaw = 90.0F;
    action.head_yaw = 90.0F;
    action.move_z = 1.0F;
    CHECK(sender.send_once(engine, action, &error));
    CHECK(sender.awaiting_feedback());
    CHECK(sender.pending_tick() == 10U);
    CHECK(backend.sent.size() == 1U);
    CHECK(backend.sent[0].packet_id == 0xfeU);

    wire::BatchCodecConfig batch_config;
    batch_config.compression_enabled = false;
    auto decoded = wire::BedrockBatchCodec::decode(backend.sent[0].payload, batch_config, nullptr);
    CHECK(decoded.has_value() && decoded->size() == 1U);
    CHECK(decoded->at(0).packet_id == wire::kPlayerAuthInputPacketId);
    auto auth = wire::PlayerAuthInputCodec::decode_payload(decoded->at(0).payload, nullptr);
    CHECK(auth.has_value() && auth->tick == 10U && auth->yaw == 90.0F);

    CHECK(!sender.send_once(engine, action, &error));
    CHECK(backend.sent.size() == 1U);
    CHECK(!sender.observe_server_tick(9U));
    CHECK(sender.awaiting_feedback());
    CHECK(sender.observe_server_tick(10U));
    CHECK(!sender.awaiting_feedback());

    action.tick = 11U;
    CHECK(sender.send_once(engine, action, &error));
    CHECK(backend.sent.size() == 2U);
}

static void test_movement_rejects_invalid_action_before_queue_or_send() {
    NetworkEngineConfig config;
    config.batch.compression_enabled = false;
    Protocol2193NetworkEngine engine(config);
    MovementBackend backend;
    std::string error;
    CHECK(engine.attach(backend, &error));
    enter_game(engine);
    wire::MovementActionSender sender;

    wire::PlayerAuthInputPacket action;
    action.tick = 1U;
    action.move_x = std::numeric_limits<float>::quiet_NaN();
    CHECK(!sender.send_once(engine, action, &error));
    CHECK(!error.empty());
    CHECK(engine.pending_game_packets() == 0U && backend.sent.empty());

    error.clear();
    action.move_x = 1.5F;
    CHECK(!sender.send_once(engine, action, &error));
    CHECK(engine.pending_game_packets() == 0U && backend.sent.empty());

    error.clear();
    action.move_x = 0.0F;
    action.item_interaction_present = true;
    CHECK(!sender.send_once(engine, action, &error));
    CHECK(engine.pending_game_packets() == 0U && backend.sent.empty());
}

static void test_movement_requires_game_state_and_monotonic_ticks() {
    NetworkEngineConfig config;
    config.batch.compression_enabled = false;
    Protocol2193NetworkEngine engine(config);
    MovementBackend backend;
    std::string error;
    CHECK(engine.attach(backend, &error));
    wire::MovementActionSender sender;
    wire::PlayerAuthInputPacket action;
    action.tick = 1U;
    CHECK(!sender.send_once(engine, action, &error));
    enter_game(engine);
    CHECK(sender.send_once(engine, action, &error));
    CHECK(sender.observe_server_tick(1U));
    CHECK(!sender.send_once(engine, action, &error));
    CHECK(backend.sent.size() == 1U);
}

int main() {
    test_movement_sends_exactly_once_until_server_tick_feedback();
    test_movement_rejects_invalid_action_before_queue_or_send();
    test_movement_requires_game_state_and_monotonic_ticks();
    std::cout << "v0.33 movement action tests: PASS\n";
    return 0;
}
