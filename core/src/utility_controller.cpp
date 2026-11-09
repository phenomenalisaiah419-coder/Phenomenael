#include "core/utility_controller.hpp"

namespace bedrockai {
UtilityCommand UtilityController::decide(const WorldSnapshot& snapshot, const UtilityInventory& inv,
                                         UtilityTrigger preferred) const {
    UtilityCommand out;
    out.trigger = preferred;

    if (snapshot.player.airborne && snapshot.player.velocity.y < -6.0 && inv.wind_charge) {
        out.kind = UtilityKind::WindCharge;
        out.trigger = UtilityTrigger::EmergencyFall;
        out.emergency = true;
        out.valid = true;
        return out;
    }
    if (snapshot.player.position.y < -50.0 && inv.pearl) {
        out.kind = UtilityKind::Pearl;
        out.trigger = UtilityTrigger::EmergencyVoid;
        out.emergency = true;
        out.valid = true;
        return out;
    }
    if (preferred == UtilityTrigger::Knockback && inv.knockback_item) {
        out.kind = UtilityKind::KnockbackItem;
        out.valid = true;
        return out;
    }
    if (preferred == UtilityTrigger::Chase && inv.pearl) {
        out.kind = UtilityKind::Pearl;
        out.valid = true;
        return out;
    }
    if (preferred == UtilityTrigger::Finish && inv.fireball) {
        out.kind = UtilityKind::Fireball;
        out.valid = true;
        return out;
    }
    return out;
}
}
