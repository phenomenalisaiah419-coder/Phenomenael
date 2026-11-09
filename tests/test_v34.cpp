#include "core/wire/movement_feedback_bridge.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace bedrockai;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAIL: " << #condition << " at line " << __LINE__ << "\n"; \
    std::exit(1); } } while (false)

namespace {
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
    TransportState state() const override {
        return connected ? TransportState::InGame : TransportState::Disconnected;
    }
    void set_receive_handler(ReceiveHandler handler) override { handler_ = std::move(handler); }

    bool connected = false;
    std::vector<TransportFrame> sent;
private:
    ReceiveHandler handler_;
};

void put_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void put_f32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        out.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xffU));
    }
}

void put_varu64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (value >= 0x80U) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7fU) | 0x80U));
        value >>= 7U;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}

void put_vari64(std::vector<std::uint8_t>& out, std::int64_t value) {
    const std::uint64_t encoded = (static_cast<std::uint64_t>(value) << 1U) ^
        static_cast<std::uint64_t>(value >> 63U);
    put_varu64(out, encoded);
}

void put_vari32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const std::uint32_t encoded = (static_cast<std::uint32_t>(value) << 1U) ^
        static_cast<std::uint32_t>(value >> 31U);
    std::uint32_t remainder = encoded;
    while (remainder >= 0x80U) {
        out.push_back(static_cast<std::uint8_t>((remainder & 0x7fU) | 0x80U));
        remainder >>= 7U;
    }
    out.push_back(static_cast<std::uint8_t>(remainder));
}

TransportFrame frame(std::uint32_t packet_id, std::vector<std::uint8_t> payload) {
    TransportFrame result;
    result.identity = {"1.26.50", 2193U, BedrockTransport::RakNet};
    result.direction = Direction::Clientbound;
    result.packet_id = packet_id;
    result.payload = std::move(payload);
    return result;
}

TransportFrame start_game() {
    std::vector<std::uint8_t> bytes;
    put_vari64(bytes, 1234); // local unique ID
    put_varu64(bytes, 42U); // local runtime ID
    put_vari32(bytes, 0); // survival
    put_f32(bytes, 10.0F);
    put_f32(bytes, 64.0F);
    put_f32(bytes, 10.0F);
    put_f32(bytes, 0.0F); // pitch
    put_f32(bytes, 0.0F); // yaw
    return frame(11U, std::move(bytes));
}

TransportFrame player_correction(std::uint64_t tick) {
    std::vector<std::uint8_t> bytes;
    put_u8(bytes, 0U); // player, not vehicle
    put_f32(bytes, 10.0F); put_f32(bytes, 64.0F); put_f32(bytes, 10.0F);
    put_f32(bytes, 0.0F); put_f32(bytes, 0.0F); put_f32(bytes, 0.0F);
    put_u8(bytes, 1U); // on ground
    put_varu64(bytes, tick);
    return frame(161U, std::move(bytes));
}

TransportFrame move_player(std::uint64_t runtime_id, std::uint64_t tick) {
    std::vector<std::uint8_t> bytes;
    put_varu64(bytes, runtime_id);
    put_f32(bytes, 10.0F); put_f32(bytes, 64.0F); put_f32(bytes, 10.0F);
    put_f32(bytes, 0.0F); put_f32(bytes, 0.0F);
    put_f32(bytes, 0.0F); // head yaw
    put_u8(bytes, 0U); // normal position mode
    put_u8(bytes, 1U); // on ground
    put_varu64(bytes, 0U); // riding runtime ID
    put_varu64(bytes, tick);
    return frame(19U, std::move(bytes));
}

TransportFrame update_game_type(std::int64_t unique_id, std::uint64_t tick) {
    std::vector<std::uint8_t> bytes;
    put_vari32(bytes, 0);
    put_vari64(bytes, unique_id);
    put_varu64(bytes, tick);
    return frame(151U, std::move(bytes));
}

void enter_game(Protocol2193NetworkEngine& engine) {
    std::string error;
    auto start = start_game();
    CHECK(engine.session().ingest(start, &error));
    CHECK(engine.state() == TransportState::InGame);
}

void queue_pending_action(Protocol2193NetworkEngine& engine, wire::MovementActionSender& sender) {
    wire::PlayerAuthInputPacket action;
    action.tick = 10U;
    action.position = {10.0, 64.0, 10.0};
    std::string error;
    CHECK(sender.send_once(engine, action, &error));
    CHECK(sender.awaiting_feedback());
}

void test_only_local_player_tick_feedback_releases_pending_action() {
    NetworkEngineConfig config;
    config.batch.compression_enabled = false;
    Protocol2193NetworkEngine engine(config);
    MovementBackend backend;
    std::string error;
    CHECK(engine.attach(backend, &error));
    enter_game(engine);

    Bedrock2193ObservationDecoder decoder;
    wire::MovementActionSender sender;
    wire::MovementFeedbackBridge bridge;
    ObservationDecodeReport report;
    bool acknowledged = false;

    CHECK(bridge.process_frame(decoder, start_game(), sender, &report, &acknowledged, &error));
    CHECK(!report.local_player_input_tick.has_value());
    queue_pending_action(engine, sender);

    CHECK(bridge.process_frame(decoder, move_player(99U, 999U), sender,
                               &report, &acknowledged, &error));
    CHECK(!acknowledged);
    CHECK(!report.local_player_input_tick.has_value());
    CHECK(sender.awaiting_feedback());

    CHECK(bridge.process_frame(decoder, update_game_type(9876, 999U), sender,
                               &report, &acknowledged, &error));
    CHECK(!acknowledged);
    CHECK(!report.local_player_input_tick.has_value());
    CHECK(sender.awaiting_feedback());

    CHECK(bridge.process_frame(decoder, player_correction(9U), sender,
                               &report, &acknowledged, &error));
    CHECK(report.local_player_input_tick == 9U);
    CHECK(!acknowledged);
    CHECK(sender.awaiting_feedback());

    CHECK(bridge.process_frame(decoder, player_correction(10U), sender,
                               &report, &acknowledged, &error));
    CHECK(report.local_player_input_tick == 10U);
    CHECK(acknowledged);
    CHECK(!sender.awaiting_feedback());
}

void test_local_move_player_tick_is_feedback_but_zero_is_not() {
    NetworkEngineConfig config;
    config.batch.compression_enabled = false;
    Protocol2193NetworkEngine engine(config);
    MovementBackend backend;
    std::string error;
    CHECK(engine.attach(backend, &error));
    enter_game(engine);

    Bedrock2193ObservationDecoder decoder;
    wire::MovementActionSender sender;
    wire::MovementFeedbackBridge bridge;
    ObservationDecodeReport report;
    bool acknowledged = false;

    CHECK(bridge.process_frame(decoder, start_game(), sender, &report, &acknowledged, &error));
    CHECK(bridge.process_frame(decoder, move_player(42U, 0U), sender,
                               &report, &acknowledged, &error));
    CHECK(!report.local_player_input_tick.has_value());

    queue_pending_action(engine, sender);
    CHECK(bridge.process_frame(decoder, move_player(42U, 9U), sender,
                               &report, &acknowledged, &error));
    CHECK(report.local_player_input_tick == 9U);
    CHECK(!acknowledged);
    CHECK(sender.awaiting_feedback());

    CHECK(bridge.process_frame(decoder, move_player(42U, 10U), sender,
                               &report, &acknowledged, &error));
    CHECK(report.local_player_input_tick == 10U);
    CHECK(acknowledged);
    CHECK(!sender.awaiting_feedback());
}

void test_malformed_feedback_does_not_unlock_pending_action() {
    NetworkEngineConfig config;
    config.batch.compression_enabled = false;
    Protocol2193NetworkEngine engine(config);
    MovementBackend backend;
    std::string error;
    CHECK(engine.attach(backend, &error));
    enter_game(engine);

    Bedrock2193ObservationDecoder decoder;
    wire::MovementActionSender sender;
    wire::MovementFeedbackBridge bridge;
    ObservationDecodeReport report;
    bool acknowledged = false;
    CHECK(bridge.process_frame(decoder, start_game(), sender, &report, &acknowledged, &error));
    queue_pending_action(engine, sender);

    auto malformed = player_correction(12U);
    malformed.payload.pop_back();
    CHECK(!bridge.process_frame(decoder, malformed, sender,
                                &report, &acknowledged, &error));
    CHECK(!acknowledged);
    CHECK(!report.local_player_input_tick.has_value());
    CHECK(sender.awaiting_feedback());
}
} // namespace

int main() {
    test_only_local_player_tick_feedback_releases_pending_action();
    test_local_move_player_tick_is_feedback_but_zero_is_not();
    test_malformed_feedback_does_not_unlock_pending_action();
    std::cout << "v0.34 movement feedback bridge tests: PASS\n";
    return 0;
}
