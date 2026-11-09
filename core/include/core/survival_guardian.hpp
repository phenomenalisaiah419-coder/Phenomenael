#pragma once

// Predictive survival (v0.35): "AI overrules me when I'm about to die".
//
// The legacy SurvivalController only reacts once the player is already falling.  The guardian
// looks AHEAD: it extrapolates where the player will be if the current inputs persist
// (MotionPredictor), scans that path for void / lava / lethal falls / unobserved drops
// (HazardScanner) and, when the path is deadly, chooses the least intrusive rescue that the
// current kit and terrain actually allow:
//
//   survival.sneak_halt       stop and sneak (sneaking also blocks walking off an edge)
//   survival.step_back        reverse the input when too close to stop
//   survival.bridge_forward   creep forward in sneak while placing the missing block(s)
//   survival.block_clutch     place a block under a falling player (needs a clickable face)
//   survival.water_clutch     bucket under a long fall (needs known ground)
//   survival.pearl_escape     throw a pearl to a known safe landing (needs CALIBRATED physics)
//
// Every option is validated (reach, clickable face, line of sight, replaceable target, protected
// blocks excluded - the guardian never clicks or replaces a bed) and returned with its score so
// the decision is explainable.  Learned effectiveness (per technique) multiplies the score.

#include "core/hazard_scan.hpp"
#include "core/projectile_solver.hpp"

#include <functional>
#include <string>
#include <vector>

namespace bedrockai {

struct GuardianKit {
    int blocks = 0;
    bool ender_pearl = false;
    bool pearl_calibrated = false;
    ProjectilePhysics pearl_physics{};
    bool water_bucket = false;
};

enum class GuardianAction : std::uint8_t { None, Halt, StepBack, BridgeForward, BlockClutch, PearlEscape, WaterClutch };
const char* guardian_action_name(GuardianAction a);

struct PlacementOrder {
    int x = 0, y = 0, z = 0;                 // cell to fill
    int support_x = 0, support_y = 0, support_z = 0;  // block whose face is clicked
    int face = 0;                            // 0=-y 1=+y 2=-z 3=+z 4=-x 5=+x (face of the support block)
    Vec3 click_point{};
};

struct GuardianOption {
    GuardianAction action = GuardianAction::None;
    std::string technique_id;
    bool feasible = false;
    double gain = 0.0;     // estimated survival probability if executed
    double score = 0.0;    // gain x learned effectiveness - cost + intent bonus
    int ticks_needed = 0;
    std::string why;
};

struct GuardianInput {
    const WorldGrid* grid = nullptr;
    Vec3 feet{};
    double vertical_velocity = 0.0;
    bool on_ground = true;
    double yaw_deg = 0.0;
    double pitch_deg = 0.0;
    double health = 20.0;
    double already_fallen = 0.0;
    double stick_forward = 0.0;   // the inputs expected to persist (human or AI)
    double stick_strafe = 0.0;
    bool sprint = false;
    bool sneak = false;
    bool jump = false;
    GuardianKit kit{};
    std::function<double(const std::string&)> effectiveness;  // learned multiplier, default 1.0
};

struct GuardianPlan {
    bool intervene = false;
    bool warning = false;         // hazard predicted but not yet close enough to override the human
    GuardianAction action = GuardianAction::None;
    std::string technique_id;
    HazardReport hazard;
    bool override_movement = false;
    double move_forward = 0.0;
    double move_strafe = 0.0;
    bool sprint = false;
    bool sneak = false;
    bool jump = false;
    bool aim_valid = false;
    double aim_yaw_deg = 0.0;
    double aim_pitch_deg = 0.0;   // Minecraft convention: positive looks down
    bool place_valid = false;
    PlacementOrder place{};
    bool use_pearl = false;
    Vec3 pearl_target{};
    bool use_water = false;       // fire the bucket this tick
    std::vector<GuardianOption> options;
    std::string reason;
};

struct GuardianConfig {
    int horizon_ticks = 24;
    double min_damage_to_act = 3.0;
    bool unknown_drop_is_danger = true;
    double placement_reach = 4.0;   // conservative until measured by the mechanics probe
    double water_reach = 4.0;
    double eye_height = 1.62;
    double pearl_range = 26.0;
    double pearl_self_damage = 5.0;
    double intent_bonus = 0.05;
    int aim_ticks = 0;              // 0 = aim and act in the same input packet (see HubConfig::emergency_same_tick_aim)
    int lead_ticks = 4;             // start overriding this many ticks before leaving support / touching the hazard
};

class SurvivalGuardian {
public:
    explicit SurvivalGuardian(GuardianConfig config = {}, KinematicsConfig kinematics = {},
                              HazardConfig hazards = {});
    GuardianPlan evaluate(const GuardianInput& input) const;
    std::vector<PathPoint> predict_path(const GuardianInput& input) const;
    const HazardScanner& scanner() const { return scanner_; }
    const GuardianConfig& config() const { return cfg_; }

private:
    GuardianConfig cfg_;
    MotionPredictor motion_;
    HazardScanner scanner_;
};

// Aim helper (Minecraft convention): yaw 0 faces +Z, positive pitch looks down.
void aim_at(const Vec3& eye, const Vec3& target, double* yaw_deg, double* pitch_deg);

// Finds a plain solid block whose face can be clicked from `eye` (reach + line of sight) to fill
// the empty cell (x,y,z).  Never selects beds or protected blocks.  False when there is none.
bool find_placement(const WorldGrid& grid, int x, int y, int z, const Vec3& eye, double reach, PlacementOrder* out);

} // namespace bedrockai
