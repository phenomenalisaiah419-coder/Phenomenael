#include "core/survival_controller.hpp"
#include "core/aim_controller.hpp"

namespace bedrockai {
SurvivalController::SurvivalController(SurvivalConfig config) : config_(config) {}

SurvivalCommand SurvivalController::decide(const WorldSnapshot& snapshot, const UtilityInventory& inventory) const {
    SurvivalCommand out;
    const bool voidish = snapshot.player.position.y <= config_.void_threshold;
    const bool falling = snapshot.player.airborne && snapshot.player.velocity.y <= config_.emergency_fall_speed;
    const bool correction = snapshot.last_correction.present && snapshot.last_correction.distance_error >= config_.correction_error;
    const bool critical = snapshot.player.health <= config_.critical_health;

    if (voidish || falling) {
        out.phase = SurvivalPhase::VoidRecovery;
        out.utility = UtilityController{}.decide(snapshot, inventory, UtilityTrigger::EmergencyVoid);
        MovementTarget target{snapshot.player.position, MovementStyle::Recover, true, false, true, 1.5, 1.0};
        out.movement = MovementController{}.compute(snapshot, target);
        const int px = static_cast<int>(snapshot.player.position.x);
        const int py = static_cast<int>(snapshot.player.position.y) - 1;
        const int pz = static_cast<int>(snapshot.player.position.z);
        out.emergency_place = BlockPlacementTarget{px, py, pz, -1, true, true};
        out.switch_item = out.utility.valid;
        out.valid = out.movement.valid;
        return out;
    }

    if (critical) {
        out.phase = SurvivalPhase::Critical;
        out.utility = UtilityController{}.decide(snapshot, inventory, UtilityTrigger::Chase);
        MovementTarget target{snapshot.player.position, MovementStyle::Retreat, true, true, false, 2.0, 1.0};
        out.movement = MovementController{}.compute(snapshot, target);
        out.switch_item = out.utility.valid;
        out.valid = out.movement.valid;
        return out;
    }

    if (correction) {
        out.phase = SurvivalPhase::Threatened;
        out.movement = MovementController{}.compute(
            snapshot,
            MovementTarget{snapshot.last_correction.authoritative_position,
                           MovementStyle::Approach, false, false, false, 2.0, 1.0});
        out.valid = out.movement.valid;
        return out;
    }

    out.phase = SurvivalPhase::Stable;
    out.valid = true;
    return out;
}
}
