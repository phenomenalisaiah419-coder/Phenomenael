#include "core/embedded_client_bridge.hpp"

#include <new>

namespace bedrockai {

EmbeddedClientControllerBridge::EmbeddedClientControllerBridge(CapabilityProfile capabilities)
    : capabilities_(capabilities) {}

bool EmbeddedClientControllerBridge::tick(const WorldSnapshot& snapshot,
                                           const InventoryState& inventory,
                                           const EmbeddedTouchInput& touch,
                                           const EmbeddedAiInput& ai,
                                           EmbeddedActionFrame& output,
                                           std::string* error) {
    if (snapshot.player.health < 0.0) {
        if (error) *error = "embedded client supplied invalid negative player health";
        return false;
    }
    TouchFrame raw_touch;
    raw_touch.attack = touch.attack;
    raw_touch.place = touch.place;
    raw_touch.jump = touch.jump;
    raw_touch.sneak = touch.sneak;
    raw_touch.sprint = touch.sprint;
    raw_touch.utility = touch.utility;
    raw_touch.explicit_override = touch.explicit_override;
    raw_touch.move_x = touch.move_x;
    raw_touch.move_y = touch.move_y;
    raw_touch.look_dx = touch.look_dx;
    raw_touch.look_dy = touch.look_dy;
    HumanInput human;
    human.touch = HumanIntentMapper{}.map(raw_touch);

    CombatCommand command;
    command.propose_attack = ai.propose_attack;
    command.propose_place = ai.propose_place;
    command.place_emergency_allowed = ai.place_emergency_allowed;
    command.propose_item = ai.propose_item;
    command.propose_utility = ai.propose_utility;
    command.utility = ai.utility;
    command.emergency = ai.emergency;
    command.movement = ai.movement;
    command.aim = ai.aim;

    SharedControlInput input{human, command};
    const auto result = controller_.compose(snapshot, inventory, input, capabilities_);

    output = EmbeddedActionFrame{};
    output.movement = result.movement.valid;
    output.look = result.aim.valid;
    output.jump = result.channels.jump;
    output.sneak = result.channels.sneak;
    output.sprint = result.channels.sprint;
    output.attack = result.channels.attack;
    output.place = result.channels.place;
    output.utility = result.channels.utility;
    output.forward = result.movement.forward;
    output.strafe = result.movement.strafe;
    output.yaw_delta = result.aim.yaw_delta;
    output.pitch_delta = result.aim.pitch_delta;
    last_output_ = output;
    ++tick_count_;
    return true;
}

void EmbeddedClientControllerBridge::reset() {
    tick_count_ = 0;
    last_output_ = EmbeddedActionFrame{};
}

} // namespace bedrockai

extern "C" {

std::uintptr_t bedrockai_embedded_create() {
    auto* bridge = new (std::nothrow) bedrockai::EmbeddedClientControllerBridge();
    return reinterpret_cast<std::uintptr_t>(bridge);
}

void bedrockai_embedded_destroy(std::uintptr_t handle) {
    delete reinterpret_cast<bedrockai::EmbeddedClientControllerBridge*>(handle);
}

int bedrockai_embedded_tick(std::uintptr_t handle, std::uint64_t tick, double health, int on_ground,
                            int airborne, const BedrockAiEmbeddedTouch* touch,
                            const BedrockAiEmbeddedAi* ai, BedrockAiEmbeddedActions* output) {
    auto* bridge = reinterpret_cast<bedrockai::EmbeddedClientControllerBridge*>(handle);
    if (!bridge || !touch || !ai || !output) return 0;
    bedrockai::WorldSnapshot snapshot;
    snapshot.tick = tick;
    snapshot.player.health = health;
    snapshot.player.on_ground = on_ground != 0;
    snapshot.player.airborne = airborne != 0;
    bedrockai::EmbeddedTouchInput t{
        touch->attack != 0, touch->place != 0, touch->jump != 0, touch->sneak != 0,
        touch->sprint != 0, touch->utility != 0, touch->explicit_override != 0,
        touch->move_x, touch->move_y, touch->look_dx, touch->look_dy
    };
    bedrockai::EmbeddedAiInput a;
    a.propose_attack = ai->propose_attack != 0;
    a.propose_place = ai->propose_place != 0;
    a.place_emergency_allowed = ai->place_emergency_allowed != 0;
    a.propose_item = ai->propose_item != 0;
    a.propose_utility = ai->propose_utility != 0;
    a.utility = static_cast<bedrockai::UtilityKind>(ai->utility);
    a.emergency = ai->emergency != 0;
    a.movement.valid = ai->movement_valid != 0;
    a.movement.forward = ai->movement_forward;
    a.movement.strafe = ai->movement_strafe;
    a.aim.valid = ai->aim_valid != 0;
    a.aim.yaw_delta = ai->aim_yaw_delta;
    a.aim.pitch_delta = ai->aim_pitch_delta;
    bedrockai::InventoryState inventory;
    bedrockai::EmbeddedActionFrame action;
    std::string error;
    if (!bridge->tick(snapshot, inventory, t, a, action, &error)) return 0;
    output->movement = action.movement ? 1 : 0;
    output->look = action.look ? 1 : 0;
    output->jump = action.jump ? 1 : 0;
    output->sneak = action.sneak ? 1 : 0;
    output->sprint = action.sprint ? 1 : 0;
    output->attack = action.attack ? 1 : 0;
    output->place = action.place ? 1 : 0;
    output->utility = action.utility ? 1 : 0;
    output->forward = action.forward;
    output->strafe = action.strafe;
    output->yaw_delta = action.yaw_delta;
    output->pitch_delta = action.pitch_delta;
    return 1;
}

void bedrockai_embedded_reset(std::uintptr_t handle) {
    auto* bridge = reinterpret_cast<bedrockai::EmbeddedClientControllerBridge*>(handle);
    if (bridge) bridge->reset();
}

}
