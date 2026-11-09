#pragma once

#include "aim_controller.hpp"
#include "arbiter.hpp"
#include "movement_controller.hpp"
#include "state.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace bedrockai {

enum class CombatStyle { Aggressive, Defensive, Strafing, Chase, Retreat, EdgeVoid, MultiTarget, Finish };
enum class CombatPhase { Search, Approach, Engage, Recover, Disengage };

struct CombatConfig {
    // These are not Bedrock mechanics constants. They must be populated from the probe.
    std::optional<double> attack_distance;
    std::optional<double> attack_interval_seconds;
    double defensive_health_fraction = 0.35;
    double finish_health = 6.0;
    double emergency_fall_speed = -8.0;
    double edge_recovery_y = -64.0;
};

struct CombatCommand {
    std::optional<std::uint64_t> target_runtime_id;
    CombatStyle style = CombatStyle::Aggressive;
    CombatPhase phase = CombatPhase::Search;
    MovementCommand movement{};
    AimCommand aim{};
    bool propose_attack = false;
    bool propose_place = false;
    bool place_emergency_allowed = false;
    bool propose_item = false;
    bool propose_utility = false;
    UtilityKind utility = UtilityKind::None;
    bool emergency = false;
    bool valid = false;
};

class CombatController {
public:
    explicit CombatController(CombatConfig config = {});

    CombatCommand decide(const WorldSnapshot& snapshot, CombatStyle style) const;

private:
    CombatConfig config_;

    const EntityState* choose_target(const WorldSnapshot& snapshot, CombatStyle style) const;
    bool target_is_attackable(const WorldSnapshot& snapshot, const EntityState& target) const;
    static double distance(const Vec3& a, const Vec3& b);
};

} // namespace bedrockai
