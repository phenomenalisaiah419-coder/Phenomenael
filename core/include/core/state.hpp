#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace bedrockai {

struct Vec3 { double x=0, y=0, z=0; };
struct Rot2 { double pitch=0, yaw=0; };

struct AttributeModifierState {
    std::string id;
    std::string name;
    double amount = 0.0;
    std::int32_t operation = 0;
    std::int32_t operand = 0;
    bool serializable = false;
};

struct ActorAttributeState {
    double min_value = 0.0;
    double max_value = 0.0;
    double current_value = 0.0;
    double default_min_value = 0.0;
    double default_max_value = 0.0;
    double default_value = 0.0;
    std::string name;
    std::vector<AttributeModifierState> modifiers;
    std::uint64_t observed_tick = 0;
};

struct MobEffectState {
    int amplifier = 0;
    std::uint32_t duration_ticks = 0;
    std::uint64_t observed_tick = 0;
    bool ambient = false;
    bool show_particles = false;
};

struct PlayerState {
    std::uint64_t runtime_id = 0;
    Vec3 position{};
    Vec3 velocity{};
    Rot2 rotation{};
    double head_yaw = 0;
    double health = 20;
    bool on_ground = false;
    bool sprinting = false;
    bool sneaking = false;
    bool airborne = false;
    int selected_slot = -1;
    std::uint64_t last_server_tick = 0;
    // Appended protocol/learning state preserves existing aggregate initializers above.
    std::int64_t unique_id = 0;
    // Updated by the action executor only after an attack input has actually been emitted.
    std::uint64_t last_attack_tick = 0;
    std::unordered_map<int, MobEffectState> effects;
    // Ground certainty is distinct from the boolean value; not all packet types carry it.
    bool ground_state_known = false;
    std::unordered_map<std::string, ActorAttributeState> attributes;
};

struct EntityState {
    // Preserve the original seven fields first for existing aggregate initializers.
    std::uint64_t runtime_id = 0;
    int type_id = 0;
    Vec3 position{};
    Vec3 velocity{};
    double health = 0;
    bool hostile = false;
    bool alive = true;
    // Versioned protocol observation fields are appended for source compatibility.
    std::int64_t unique_id = 0;
    std::string type_identifier;
    std::string player_name;
    std::string player_uuid;
    bool is_player = false;
    bool hostility_known = false;
    bool on_ground = false;
    std::uint64_t last_update_tick = 0;
    std::unordered_map<int, MobEffectState> effects;
    Rot2 rotation{};
    double head_yaw = 0.0;
    bool ground_state_known = false;
    std::unordered_map<std::string, ActorAttributeState> attributes;

    EntityState() = default;
    EntityState(std::uint64_t runtime_id_value, int type_id_value, Vec3 position_value,
                Vec3 velocity_value, double health_value, bool hostile_value, bool alive_value)
        : runtime_id(runtime_id_value), type_id(type_id_value), position(position_value),
          velocity(velocity_value), health(health_value), hostile(hostile_value), alive(alive_value) {}
};

struct BlockObservation {
    int x=0, y=0, z=0;
    int runtime_id = 0;
    bool solid = false;
    bool replaceable = false;
    std::uint64_t observed_tick = 0;
};

struct WorldState {
    int dimension = 0;
    int game_type = -1;
    std::unordered_map<std::uint64_t, BlockObservation> blocks;
    std::unordered_map<std::uint64_t, EntityState> entities;
};

struct ServerCorrection {
    Vec3 authoritative_position{};
    Vec3 authoritative_velocity{};
    Rot2 authoritative_rotation{};
    std::uint64_t server_tick = 0;
    double distance_error = 0;
    bool present = false;
};

struct WorldSnapshot {
    PlayerState player{};
    WorldState world{};
    ServerCorrection last_correction{};
    // tick is a local observation sequence. server_tick is the protocol PlayerInputTick
    // and must be used for tick-window mechanics such as learned Java-style attack cadence.
    std::uint64_t tick = 0;
    std::uint64_t server_tick = 0;
    bool server_tick_known = false;
    bool joined = false;
};

inline std::uint64_t block_key(int x, int y, int z) {
    const auto ux = static_cast<std::uint64_t>(static_cast<std::int32_t>(x));
    const auto uy = static_cast<std::uint64_t>(static_cast<std::int32_t>(y));
    const auto uz = static_cast<std::uint64_t>(static_cast<std::int32_t>(z));
    return (ux << 42) ^ (uz << 20) ^ (uy & 0xFFFFFu);
}

} // namespace bedrockai
