#pragma once

#include "arbiter.hpp"
#include "state.hpp"
#include "utility_controller.hpp"
#include "movement_controller.hpp"
#include "aim_controller.hpp"
#include "bridge_controller.hpp"

#include <optional>
#include <string>

namespace bedrockai {

enum class SurvivalPhase { Stable, Threatened, Critical, VoidRecovery, BedlessSurvival, DeathImminent };

struct SurvivalConfig {
    double critical_health = 4.0;
    double emergency_fall_speed = -7.0;
    double void_threshold = -50.0;
    double correction_error = 1.5;
};

struct SurvivalCommand {
    SurvivalPhase phase = SurvivalPhase::Stable;
    MovementCommand movement{};
    AimCommand aim{};
    UtilityCommand utility{};
    std::optional<BlockPlacementTarget> emergency_place;
    bool switch_item = false;
    bool valid = false;
};

class SurvivalController {
public:
    explicit SurvivalController(SurvivalConfig config = {});
    SurvivalCommand decide(const WorldSnapshot& snapshot, const UtilityInventory& inventory) const;
private:
    SurvivalConfig config_;
};

} // namespace bedrockai
