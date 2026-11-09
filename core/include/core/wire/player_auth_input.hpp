#pragma once

#include "../state.hpp"
#include "../shared_control.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bedrockai::wire {

constexpr std::uint32_t kPlayerAuthInputPacketId = 144;
constexpr std::size_t kInputFlagCount = 69;

struct WireVec2 { float x = 0; float y = 0; };

struct PlayerAuthInputPacket {
    float pitch = 0;
    float yaw = 0;
    Vec3 position{};
    float move_x = 0;
    float move_z = 0;
    float head_yaw = 0;
    std::vector<std::int32_t> input_flags;
    std::uint32_t input_mode = 2;       // Touch
    std::uint32_t play_mode = 0;        // Normal
    std::int32_t interaction_model = 0; // Touch
    float interact_pitch = 0;
    float interact_yaw = 0;
    std::uint64_t tick = 0;
    Vec3 delta{};
    WireVec2 analogue_move{};
    Vec3 camera_orientation{};
    WireVec2 raw_move{};
    bool item_interaction_present = false;
    bool item_stack_request_present = false;
    bool block_actions_present = false;
    bool vehicle_rotation_present = false;
    bool client_predicted_vehicle_present = false;
};

struct WireError {
    std::string message;
    std::size_t offset = 0;
};

class PlayerAuthInputCodec {
public:
    // Encodes the protocol-2193 PlayerAuthInput payload, excluding the outer
    // RakNet/game-packet batching layers. Unsupported optional payloads are
    // rejected rather than guessed.
    static std::optional<std::vector<std::uint8_t>> encode_payload(
        const PlayerAuthInputPacket& packet, WireError* error = nullptr);

    static std::optional<PlayerAuthInputPacket> decode_payload(
        const std::vector<std::uint8_t>& bytes, WireError* error = nullptr);

    // Prepends the Bedrock packet ID as a varuint32 to the payload. This is
    // still below RakNet/NetherNet and compression/encryption framing.
    static std::optional<std::vector<std::uint8_t>> encode_game_packet(
        const PlayerAuthInputPacket& packet, WireError* error = nullptr);
};

PlayerAuthInputPacket make_player_auth_input(
    const WorldSnapshot& snapshot,
    const MovementCommand& movement,
    const AimCommand& aim,
    bool human_attack_held,
    bool human_place_held);

} // namespace bedrockai::wire
