#pragma once

#include "shared_control.hpp"

#include <cstdint>
#include <string>

namespace bedrockai {

struct EmbeddedTouchInput {
    bool attack = false;
    bool place = false;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
    bool utility = false;
    bool explicit_override = false;
    double move_x = 0;
    double move_y = 0;
    double look_dx = 0;
    double look_dy = 0;
};

struct EmbeddedAiInput {
    bool propose_attack = false;
    bool propose_place = false;
    bool place_emergency_allowed = false;
    bool propose_item = false;
    bool propose_utility = false;
    UtilityKind utility = UtilityKind::None;
    bool emergency = false;
    MovementCommand movement{};
    AimCommand aim{};
};

struct EmbeddedActionFrame {
    bool movement = false;
    bool look = false;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
    bool attack = false;
    bool place = false;
    bool utility = false;
    double forward = 0;
    double strafe = 0;
    double yaw_delta = 0;
    double pitch_delta = 0;
};

// Stable in-process boundary for a modified/embedded Bedrock client. The game
// process calls tick() from its normal client tick; no second player entity is created.
class EmbeddedClientControllerBridge {
public:
    explicit EmbeddedClientControllerBridge(CapabilityProfile capabilities = {});

    bool tick(const WorldSnapshot& snapshot,
              const InventoryState& inventory,
              const EmbeddedTouchInput& touch,
              const EmbeddedAiInput& ai,
              EmbeddedActionFrame& output,
              std::string* error = nullptr);

    std::uint64_t tick_count() const { return tick_count_; }
    const EmbeddedActionFrame& last_output() const { return last_output_; }
    void reset();

private:
    CapabilityProfile capabilities_{};
    SharedControlController controller_{};
    std::uint64_t tick_count_ = 0;
    EmbeddedActionFrame last_output_{};
};

} // namespace bedrockai

extern "C" {
struct BedrockAiEmbeddedTouch {
    int attack, place, jump, sneak, sprint, utility, explicit_override;
    double move_x, move_y, look_dx, look_dy;
};
struct BedrockAiEmbeddedAi {
    int propose_attack, propose_place, place_emergency_allowed, propose_item, propose_utility;
    int utility;
    int emergency;
    int movement_valid;
    double movement_forward, movement_strafe;
    int aim_valid;
    double aim_yaw_delta, aim_pitch_delta;
};
struct BedrockAiEmbeddedActions {
    int movement, look, jump, sneak, sprint, attack, place, utility;
    double forward, strafe, yaw_delta, pitch_delta;
};

std::uintptr_t bedrockai_embedded_create();
void bedrockai_embedded_destroy(std::uintptr_t handle);
int bedrockai_embedded_tick(std::uintptr_t handle,
                            std::uint64_t tick,
                            double health,
                            int on_ground,
                            int airborne,
                            const BedrockAiEmbeddedTouch* touch,
                            const BedrockAiEmbeddedAi* ai,
                            BedrockAiEmbeddedActions* output);
void bedrockai_embedded_reset(std::uintptr_t handle);
}
