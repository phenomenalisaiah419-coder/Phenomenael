#pragma once

// Closed-loop test simulator (v0.35).
//
// A deterministic mini-world used to test the CONTROL LOGIC end to end: takeover, predictive
// survival, bridging, pearls, bucket clutches, fights.  It deliberately reuses the
// MotionPredictor kinematics for movement, so it validates sequencing, timing budgets and policy
// gates - it does NOT prove that real Minecraft physics match.  Real-game calibration comes from
// the mechanics probe on a controlled server (see docs/AUDIT_V035.md, "what the simulator is not").
//
// The simulator enforces its own, independent action rules (reach, adjacency, line of aim,
// replaceable cell, no placing inside the player) so a bad plan is rejected here instead of
// silently "working".

#include "core/hazard_scan.hpp"
#include "core/survival_guardian.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace bedrockai {

struct SimConfig {
    double void_kill_y = -64.0;
    double pearl_damage = 5.0;
    double place_reach = 4.5;          // looser than the guardian's conservative 4.0 on purpose
    double max_health = 20.0;
    double attack_reach = 3.0;
    double attack_damage = 6.0;
    int attack_cooldown_ticks = 10;
    double pearl_speed = 1.5;          // blocks per tick of flight
    double aim_tolerance_deg = 8.0;
    double knockback = 0.55;
};

struct SimInput {
    double forward = 0.0;
    double strafe = 0.0;
    double yaw_deg = 0.0;
    double pitch_deg = 0.0;
    bool sprint = false;
    bool sneak = false;
    bool jump = false;
    bool attack = false;
    bool place = false;
    PlacementOrder place_order{};
    bool throw_pearl = false;
    Vec3 pearl_target{};
    bool use_water = false;
    PlacementOrder water_order{};
};

enum class EnemyStyle : std::uint8_t { Rusher, Strafer, Camper };

struct SimEnemy {
    std::string id;
    EnemyStyle style = EnemyStyle::Rusher;
    Vec3 pos{};
    double health = 20.0;
    double speed = 0.23;
    double damage = 3.0;
    int attack_period = 14;
    int attack_cooldown = 0;
    double reach = 2.6;
    int strafe_period = 10;
    double strafe_speed = 0.18;
    double strafe_dir = 1.0;
    bool alive() const { return health > 0.0; }
};

struct SimPlayer {
    Vec3 pos{};
    double vy = 0.0;
    bool on_ground = true;
    double health = 20.0;
    bool dead = false;
    std::string death_cause;
    int blocks = 0;
    int pearls = 0;
    bool water_bucket = false;
    double fall_distance = 0.0;
    double yaw = 0.0;
    double pitch = 0.0;
    int attack_cooldown = 0;
};

class SimWorld {
public:
    explicit SimWorld(SimConfig config = {});
    WorldGrid grid;
    SimPlayer player;
    std::vector<SimEnemy> enemies;
    std::uint64_t tick = 0;
    double damage_dealt = 0.0;
    double damage_taken = 0.0;
    int blocks_placed = 0;
    std::vector<std::string> rejected;  // reasons for rejected actions (tests assert on these)

    void step(const SimInput& input);
    const SimConfig& config() const { return cfg_; }
    bool standing_on(BlockClass c) const;

private:
    SimConfig cfg_;
    MotionPredictor motion_;
    struct Pearl {
        bool active = false;
        Vec3 target{};
        int ticks_left = 0;
    } pearl_;
    Vec3 knock_{};
    int knock_ticks_ = 0;

    bool look_matches(const Vec3& eye, const Vec3& point, double yaw, double pitch, double tol) const;
    void hurt(double amount, const char* cause);
    void apply_actions(const SimInput& in);
    void move_player(const SimInput& in);
    void move_enemies();
};

} // namespace bedrockai
