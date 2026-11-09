#include "core/wire/movement_action_sender.hpp"

#include <cmath>

namespace bedrockai::wire {
namespace {
void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

bool bounded(double value, double minimum, double maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}
}

bool MovementActionSender::send_once(Protocol2193NetworkEngine& engine,
                                     const PlayerAuthInputPacket& packet,
                                     std::string* error) {
    if (engine.state() != TransportState::InGame) {
        set_error(error, "movement action requires an in-game session");
        return false;
    }
    if (pending_tick_.has_value()) {
        set_error(error, "previous movement action is awaiting server feedback");
        return false;
    }
    if ((has_submitted_ && packet.tick <= last_submitted_tick_) || packet.tick == 0U) {
        set_error(error, "movement action tick must increase monotonically");
        return false;
    }
    if (!bounded(packet.pitch, -90.0F, 90.0F) ||
        !bounded(packet.yaw, -180.0F, 180.0F) ||
        !bounded(packet.head_yaw, -180.0F, 180.0F) ||
        !bounded(packet.move_x, -1.0F, 1.0F) ||
        !bounded(packet.move_z, -1.0F, 1.0F) ||
        !bounded(packet.interact_pitch, -90.0F, 90.0F) ||
        !bounded(packet.interact_yaw, -180.0F, 180.0F) ||
        !bounded(packet.analogue_move.x, -1.0F, 1.0F) ||
        !bounded(packet.analogue_move.y, -1.0F, 1.0F) ||
        !bounded(packet.raw_move.x, -1.0F, 1.0F) ||
        !bounded(packet.raw_move.y, -1.0F, 1.0F) ||
        !bounded(packet.camera_orientation.x, -1.01, 1.01) ||
        !bounded(packet.camera_orientation.y, -1.01, 1.01) ||
        !bounded(packet.camera_orientation.z, -1.01, 1.01) ||
        !bounded(packet.position.x, -30000000.0F, 30000000.0F) ||
        !bounded(packet.position.y, -2048.0F, 2048.0F) ||
        !bounded(packet.position.z, -30000000.0F, 30000000.0F) ||
        !bounded(packet.delta.x, -1000.0F, 1000.0F) ||
        !bounded(packet.delta.y, -1000.0F, 1000.0F) ||
        !bounded(packet.delta.z, -1000.0F, 1000.0F)) {
        set_error(error, "movement action contains non-finite or out-of-range movement values");
        return false;
    }
    if (packet.item_interaction_present || packet.item_stack_request_present ||
        packet.block_actions_present || packet.vehicle_rotation_present ||
        packet.client_predicted_vehicle_present) {
        set_error(error, "movement-only sender refuses optional interaction payloads");
        return false;
    }
    if (engine.pending_game_packets() != 0U) {
        set_error(error, "network batch already contains queued packets");
        return false;
    }

    WireError wire_error;
    auto payload = PlayerAuthInputCodec::encode_payload(packet, &wire_error);
    if (!payload.has_value()) {
        if (error != nullptr) *error = wire_error.message;
        return false;
    }

    // Lock the action before attempting transport send. If send fails ambiguously,
    // don't blindly resend and risk duplicating an action the server received.
    if (!engine.queue_game_packet(kPlayerAuthInputPacketId, std::move(*payload), error)) {
        return false;
    }
    pending_tick_ = packet.tick;
    last_submitted_tick_ = packet.tick;
    has_submitted_ = true;
    if (!engine.flush_batch(error)) {
        return false;
    }
    return true;
}

bool MovementActionSender::observe_server_tick(std::uint64_t server_tick) {
    if (!pending_tick_.has_value() || server_tick < *pending_tick_) return false;
    pending_tick_.reset();
    return true;
}

void MovementActionSender::reset() {
    pending_tick_.reset();
    last_submitted_tick_ = 0U;
    has_submitted_ = false;
}

} // namespace bedrockai::wire
