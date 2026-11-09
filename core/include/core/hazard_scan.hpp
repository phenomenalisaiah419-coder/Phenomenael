#pragma once

// Predictive hazard analysis (v0.35).
//
// MotionPredictor extrapolates where the player WILL be if the current inputs persist
// ("input persistence" model, deliberately conservative: it overestimates how far a
// human keeps going, so interventions come early rather than late).
// HazardScanner walks that path against a WorldGrid and reports the first dangerous
// position: void, lava, harmful block, lethal/damaging fall, or an unobserved drop.
//
// Conventions (same as MovementController): yaw 0 faces +Z, forward vector is
// (-sin yaw, cos yaw), strafe vector is (cos yaw, sin yaw).  Positions are FEET positions.
// All physical constants are model parameters that must be confirmed by the mechanics probe.

#include "core/world_grid.hpp"

#include <vector>

namespace bedrockai {

struct KinematicsConfig {
    double walk_per_tick = 0.2158;    // 4.317 blocks/s
    double sprint_per_tick = 0.2806;  // 5.612 blocks/s
    double sneak_per_tick = 0.0647;   // 1.295 blocks/s
    double jump_velocity = 0.42;
    double gravity_per_tick = 0.08;
    double air_drag = 0.98;
    double half_width = 0.3;
};

struct PathPoint {
    Vec3 pos{};
    bool airborne = false;
    double vy = 0.0;
    BlockClass landed = BlockClass::Unknown;  // surface class when this tick is a landing
};

class MotionPredictor {
public:
    explicit MotionPredictor(KinematicsConfig config = {});
    std::vector<PathPoint> predict(const WorldGrid& grid, const Vec3& feet, double vertical_velocity,
                                   bool on_ground, double yaw_deg, double stick_forward,
                                   double stick_strafe, bool sprint, bool sneak, bool jump,
                                   int ticks) const;
    const KinematicsConfig& config() const { return config_; }

private:
    KinematicsConfig config_;
};

enum class HazardKind : std::uint8_t { None, Void, Lava, HarmfulBlock, LethalFall, DamagingFall, UnknownDrop };

struct HazardConfig {
    double max_free_fall = 3.0;  // blocks of fall before damage starts (probe-confirmable)
    double half_width = 0.3;
    double harmful_block_damage = 1.0;
};

struct HazardReport {
    bool found = false;
    HazardKind kind = HazardKind::None;
    int tick = -1;                 // path index where the hazardous run begins (0 = now)
    int impact_tick = -1;          // path index of the landing/impact; -1 = beyond the horizon
    Vec3 position{};               // where the hazard begins
    double fall_blocks = 0.0;
    double expected_damage = 0.0;
    bool lethal = false;
    bool certain = true;           // false when the verdict rests on unobserved cells
    BlockClass landing = BlockClass::Unknown;
    int landing_y = 0;             // cell y of the landing block (valid when landing != Unknown)
};

struct ColumnProbe {
    enum class Kind : std::uint8_t { Support, Liquid, Soft, Void, Unknown };
    Kind kind = Kind::Unknown;
    int cell_y = 0;
    BlockClass cls = BlockClass::Unknown;
};

class HazardScanner {
public:
    explicit HazardScanner(HazardConfig config = {});
    double fall_damage(double blocks) const;
    // First landing/obstruction below the AABB at `feet` (best of the four corner columns).
    ColumnProbe probe_below(const WorldGrid& grid, const Vec3& feet) const;
    HazardReport scan_path(const WorldGrid& grid, const std::vector<PathPoint>& path, double health,
                           double already_fallen = 0.0) const;
    const HazardConfig& config() const { return config_; }

private:
    HazardConfig config_;
    ColumnProbe probe_column(const WorldGrid& grid, int cx, int cz, int start_y) const;
};

} // namespace bedrockai
