#pragma once

#include "arbiter.hpp"
#include "state.hpp"

#include <optional>

namespace bedrockai {

enum class UtilityTrigger { None, EmergencyVoid, EmergencyFall, Chase, Knockback, Finish };

struct UtilityInventory {
    bool pearl = false;
    bool wind_charge = false;
    bool fireball = false;
    bool potion = false;
    bool knockback_item = false;
    bool flight = false;
};

struct UtilityCommand {
    UtilityKind kind = UtilityKind::None;
    UtilityTrigger trigger = UtilityTrigger::None;
    bool emergency = false;
    bool valid = false;
};

class UtilityController {
public:
    UtilityCommand decide(const WorldSnapshot& snapshot, const UtilityInventory& inventory,
                          UtilityTrigger preferred = UtilityTrigger::None) const;
};

} // namespace bedrockai
