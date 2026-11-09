#pragma once

#include "../state.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bedrockai::wire {

constexpr std::uint32_t kInventoryTransactionPacketId = 30;
constexpr std::int32_t kUseItemOnEntityTransaction = 3;
constexpr std::int32_t kAttackAction = 1;

struct AttackTransaction {
    std::uint64_t target_runtime_id = 0;
    std::int32_t hotbar_slot = 0;
    std::int32_t held_network_id = 0;
    std::uint16_t held_count = 0;
    std::uint32_t held_metadata = 0;
    std::int32_t held_stack_network_id = 0;
    Vec3 position{};
    Vec3 clicked_position{};
};

class InventoryAttackCodec {
public:
    // Protocol-2193 InventoryTransaction, UseItemOnEntity/Attack. Empty-hand
    // and ordinary item metadata are supported; arbitrary item NBT is not
    // synthesized by this layer.
    static std::optional<std::vector<std::uint8_t>> encode_attack_payload(
        const AttackTransaction& attack, std::string* error = nullptr);

    static std::optional<std::vector<std::uint8_t>> encode_attack_game_packet(
        const AttackTransaction& attack, std::string* error = nullptr);
};

} // namespace bedrockai::wire
