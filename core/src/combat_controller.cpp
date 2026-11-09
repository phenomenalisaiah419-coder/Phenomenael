#include "core/combat_controller.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bedrockai {

CombatController::CombatController(CombatConfig config) : config_(std::move(config)) {}

double CombatController::distance(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

const EntityState* CombatController::choose_target(const WorldSnapshot& snapshot, CombatStyle style) const {
    const EntityState* best = nullptr;
    double best_score = -std::numeric_limits<double>::infinity();
    for (const auto& [id, entity] : snapshot.world.entities) {
        (void)id;
        if (!entity.alive || !entity.hostile) continue;
        const double d = distance(snapshot.player.position, entity.position);
        if (!std::isfinite(d)) continue;

        double score = 100.0 - d * 8.0;
        if (style == CombatStyle::Finish) score += std::max(0.0, config_.finish_health - entity.health) * 12.0;
        if (style == CombatStyle::MultiTarget) score += std::max(0.0, 20.0 - entity.health);
        if (style == CombatStyle::EdgeVoid) score += entity.position.y < snapshot.player.position.y ? 8.0 : 0.0;
        if (score > best_score) {
            best_score = score;
            best = &entity;
        }
    }
    return best;
}

bool CombatController::target_is_attackable(const WorldSnapshot& snapshot, const EntityState& target) const {
    if (!config_.attack_distance.has_value()) return false;
    return distance(snapshot.player.position, target.position) <= *config_.attack_distance;
}

CombatCommand CombatController::decide(const WorldSnapshot& snapshot, CombatStyle style) const {
    CombatCommand out;
    out.style = style;
    const bool falling = snapshot.player.airborne && snapshot.player.velocity.y <= config_.emergency_fall_speed;
    const bool low_position = snapshot.player.position.y <= config_.edge_recovery_y;
    if (falling || low_position) {
        out.phase = CombatPhase::Recover;
        out.emergency = true;
        MovementTarget recover{{snapshot.player.position.x, snapshot.player.position.y + 2.0, snapshot.player.position.z}, MovementStyle::Recover, true, false, true, 2.5, 1};
        out.movement = MovementController{}.compute(snapshot, recover);
        out.valid = out.movement.valid;
        return out;
    }

    const EntityState* target = choose_target(snapshot, style);
    if (!target) {
        out.phase = CombatPhase::Search;
        out.valid = true;
        return out;
    }
    out.target_runtime_id = target->runtime_id;

    const double d = distance(snapshot.player.position, target->position);
    const bool close = target_is_attackable(snapshot, *target);
    const bool defensive = snapshot.player.health <= 20.0 * config_.defensive_health_fraction;
    const bool finish = style == CombatStyle::Finish && target->health <= config_.finish_health;
    out.phase = (close || finish) ? CombatPhase::Engage : CombatPhase::Approach;

    MovementStyle move_style = MovementStyle::Approach;
    bool allow_sprint = true;
    bool allow_sneak = false;
    if (style == CombatStyle::Retreat || defensive) {
        move_style = MovementStyle::Retreat;
        allow_sprint = true;
    } else if (style == CombatStyle::Strafing || style == CombatStyle::MultiTarget) {
        move_style = MovementStyle::Circle;
    } else if (style == CombatStyle::EdgeVoid) {
        move_style = MovementStyle::Retreat;
        allow_sneak = true;
    } else if (style == CombatStyle::Chase || style == CombatStyle::Aggressive || style == CombatStyle::Finish) {
        move_style = MovementStyle::Approach;
    }

    const double radius = (style == CombatStyle::Defensive || defensive) ? 3.5 : 2.5;
    MovementTarget movement_target{target->position, move_style, true, allow_sprint, allow_sneak, radius, 1.0};
    out.movement = MovementController{}.compute(snapshot, movement_target);
    out.aim = AimController{}.compute(snapshot, AimTarget{target->position, target->velocity, true, 0.0});
    out.propose_attack = close && !defensive;
    if (style == CombatStyle::Finish) out.propose_attack = close || finish;
    out.valid = out.movement.valid && out.aim.valid && d >= 0.0;
    return out;
}

} // namespace bedrockai
