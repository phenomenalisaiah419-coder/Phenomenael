#include "core/shared_control.hpp"

#include <cmath>

namespace bedrockai {

SharedControlOutput SharedControlController::compose(const WorldSnapshot&,
                                                      const InventoryState& inventory,
                                                      const SharedControlInput& input,
                                                      const CapabilityProfile& capabilities) const {
    SharedControlOutput out;
    AiProposal proposal;
    proposal.mode = input.ai.emergency ? ControlMode::Emergency : ControlMode::Takeover;
    proposal.priority = input.ai.emergency ? Priority::ImmediateSurvival : Priority::Combat;
    proposal.emergency = input.ai.emergency;
    if (input.ai.movement.valid) proposal.movement = Action{ActionKind::Move, input.ai.emergency};
    if (input.ai.aim.valid) proposal.look = Action{ActionKind::Look, input.ai.emergency};
    if (input.ai.propose_attack) proposal.attack = Action{ActionKind::Attack, input.ai.emergency};
    if (input.ai.propose_place) proposal.place = Action{ActionKind::PlaceBlock, input.ai.place_emergency_allowed};
    if (input.ai.propose_item) proposal.item = Action{ActionKind::SwitchItem, input.ai.emergency};
    if (input.ai.propose_utility) proposal.utility = Action{ActionKind::Pearl, input.ai.emergency, input.ai.utility};

    out.channels = arbitrate(capabilities, inventory, input.human, proposal);
    const bool human_override = input.human.touch.explicit_override;
    if (human_override) return out;

    // Per-channel human ownership: a live analog touch on movement/look wins over AI,
    // while a missing human movement/look input leaves AI takeover active.
    if (std::abs(input.human.touch.move_x) > 0.001 || std::abs(input.human.touch.move_y) > 0.001) {
        out.movement.forward = input.human.touch.move_y;
        out.movement.strafe = input.human.touch.move_x;
        out.movement.valid = true;
    } else if (out.channels.movement) {
        out.movement = input.ai.movement;
    }

    if (std::abs(input.human.touch.look_dx) > 0.001 || std::abs(input.human.touch.look_dy) > 0.001) {
        out.aim.yaw_delta = input.human.touch.look_dx;
        out.aim.pitch_delta = input.human.touch.look_dy;
        out.aim.valid = true;
    } else if (out.channels.look) {
        out.aim = input.ai.aim;
    }
    return out;
}

} // namespace bedrockai
