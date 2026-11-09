#pragma once

// ControlHub (v0.35): the shared-control brain that ties the new modules together.
//
//   policy gate -> intent prediction -> survival guardian -> takeover authority
//   -> (emergency rescue | AI tactical/bridge driver | human pass-through)
//   -> attack/place/utility gating through the existing arbiter -> command for the host.
//
// One tick in, one command out.  The hub never talks to a socket: the host applies the returned
// HubCommand through a verified action path (movement/PlayerAuthInput today, item-use and
// block-action codecs still to come, see docs/NEXT_STAGE_V035.md).
//
// Contract with the host:
//   * Positions are FEET positions.  Bedrock sends eye-level positions: convert at the decoder.
//   * move_forward / move_strafe are relative to look_yaw_deg when look_override is true, and
//     to the current yaw otherwise (PlayerAuthInput interprets the move vector against the
//     rotation sent in the same packet).
//   * Nothing is attacked unless an attack reach was supplied from the MEASURED profile.

#include "core/arbiter.hpp"
#include "core/automation_policy.hpp"
#include "core/intent_predictor.hpp"
#include "core/item_learner.hpp"
#include "core/opponent_model.hpp"
#include "core/skill_learner.hpp"
#include "core/survival_guardian.hpp"
#include "core/takeover_authority.hpp"

#include <optional>
#include <string>
#include <vector>

namespace bedrockai {

struct HubEnemy {
    std::string id;
    Vec3 pos{};
    double health = 20.0;
    bool hostile = true;
};

struct HubInput {
    std::uint64_t tick = 0;
    Vec3 feet{};
    double vertical_velocity = 0.0;
    bool on_ground = true;
    double yaw_deg = 0.0;
    double pitch_deg = 0.0;
    double health = 20.0;
    double max_health = 20.0;
    const WorldGrid* grid = nullptr;
    HumanIntentFrame human{};
    GuardianKit kit{};
    std::vector<HubEnemy> enemies;
    std::string server_key;
    ServerClass server_class = ServerClass::Unknown;
    AssistLevel requested_level = AssistLevel::Assist;
    GameEdition edition = GameEdition::Bedrock;
};

struct HubCommand {
    double move_forward = 0.0;
    double move_strafe = 0.0;
    bool sprint = false;
    bool sneak = false;
    bool jump = false;
    bool look_override = false;
    double look_yaw_deg = 0.0;
    double look_pitch_deg = 0.0;
    bool attack = false;
    bool place = false;
    PlacementOrder place_order{};
    bool throw_pearl = false;
    Vec3 pearl_target{};
    bool use_water = false;
    PlacementOrder water_order{};
};

struct HubOutput {
    AuthorityDecision authority;
    HubCommand cmd;
    GuardianPlan guardian;
    AssistLevel effective_level = AssistLevel::Off;
    std::string technique;      // technique currently executing (combat / bridge / survival)
    std::string explanation;
    std::vector<std::string> denials;
};

struct HubConfig {
    TakeoverConfig takeover{};
    GuardianConfig guardian{};
    SkillLearnerConfig learner{};
    ItemLearnerConfig items{};
    OpponentModelConfig opponents{};
    std::optional<double> attack_reach;   // MEASURED reach; unset = the AI never attacks
    int attack_interval_ticks = 10;       // conservative upper bound on cadence until measured
    int segment_ticks = 60;               // combat evaluation window for outcome learning
    double engage_range = 12.0;
    double retreat_health_fraction = 0.35;
    double max_turn_deg_per_tick = 45.0;
    double aim_tolerance_deg = 6.0;
    double look_to_degrees = 1.0;         // host units of HumanIntentFrame::look_dx -> degrees of yaw
    // Bedrock's PlayerAuthInput carries rotation and block/item actions in the same packet, so an
    // emergency can aim and act in one tick.  Set false to require one tick of alignment first.
    bool emergency_same_tick_aim = true;
    double success_reward = 0.55;
    std::string situation_pvp = "pvp.duel";
};

class ControlHub {
public:
    explicit ControlHub(HubConfig config = {});
    HubOutput tick(const HubInput& input);

    TakeoverAuthority& authority() { return authority_; }
    SkillLearner& learner() { return learner_; }
    OpponentModel& opponents() { return opponents_; }
    ItemLearner& items() { return items_; }
    AutomationPolicy& policy() { return policy_; }
    HumanIntentPredictor& predictor() { return predictor_; }
    CapabilityProfile& capabilities() { return caps_; }
    const std::string& current_technique() const { return technique_; }

private:
    HubConfig cfg_;
    TakeoverAuthority authority_;
    SkillLearner learner_;
    OpponentModel opponents_;
    ItemLearner items_;
    AutomationPolicy policy_;
    HumanIntentPredictor predictor_;
    SurvivalGuardian guardian_;
    CapabilityProfile caps_;

    bool mode_initialised_ = false;
    // The human's own camera yaw.  While the AI aims the camera, "forward" on the human's stick must
    // still mean "toward where I was heading", so intent is measured against the last human-owned yaw.
    double intent_yaw_ = 0.0;
    bool have_intent_yaw_ = false;
    bool movement_from_human_ = true;   // false once an AI driver or guardian override set the movement
    HubCommand last_cmd_{};
    bool last_ai_driving_ = false;
    std::optional<double> last_ai_heading_;
    double fall_distance_ = 0.0;
    double last_y_ = 0.0;
    bool have_last_y_ = false;
    double last_health_ = 0.0;
    bool have_last_health_ = false;
    std::uint64_t last_attack_tick_ = 0;
    bool attacked_once_ = false;

    // combat learning segment
    std::string technique_;
    std::string segment_target_;
    std::uint64_t segment_start_ = 0;
    double segment_dealt_ = 0.0;
    double segment_taken_ = 0.0;
    double segment_target_health_ = 0.0;
    bool segment_has_target_health_ = false;
    bool segment_attack_input_ = false;  // the human held attack at least once in this segment

    // guardian episode learning
    bool episode_active_ = false;
    std::string episode_technique_;
    double episode_start_health_ = 0.0;
    std::string episode_server_;

    HubCommand passthrough(const HubInput& in) const;
    void apply_guardian(const HubInput& in, const GuardianPlan& plan, HubOutput* out);
    void drive_combat(const HubInput& in, const HubEnemy& target, HubOutput* out);
    bool drive_bridge(const HubInput& in, HubOutput* out);
    bool next_bridge_cell(const HubInput& in, int* cx, int* cy, int* cz) const;
    void end_segment(const HubInput& in, bool force);
    void note_guardian_episode(const HubInput& in, const HubOutput& out);
    const HubEnemy* pick_target(const HubInput& in) const;
    SkillContext combat_context(const HubInput& in, const std::string& opp) const;
};

} // namespace bedrockai
