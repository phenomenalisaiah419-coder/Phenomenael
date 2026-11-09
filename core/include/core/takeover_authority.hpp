#pragma once

// Who owns the character right now (v0.35).
//
// Implements the owner's shared-control contract:
//   * While the AI is driving, the FIRST deliberate human counter-input ("I turn to run")
//     does NOT take control: the AI keeps driving and the contest is remembered.
//   * A SECOND, separate deliberate input inside the contest window hands control to the
//     human ("change of plan").  Holding one gesture never counts twice.
//   * Survival is different: an emergency request from the SurvivalGuardian overlays any state
//     (including Human) and the AI acts independently until the danger has cleared, then the
//     previous state is restored.  This is the "AI overrules me when I'm about to die" rule.
//   * The dedicated explicit-override button (P0) beats everything, emergency included.
//   * After the human took control, the AI may re-engage only after a hold period, and only by
//     (a) a confident predicted intent (Assist mode) or (b) having a plan (Takeover mode).

#include "core/intent_predictor.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace bedrockai {

enum class UserMode : std::uint8_t {
    Off,        // AI fully disabled (panic state)
    GuardOnly,  // human drives; AI intervenes only to prevent death
    Assist,     // human primary; AI continues recognised actions (attack/place follow-through)
    Takeover    // AI primary whenever it has a plan
};

enum class Authority : std::uint8_t { Human, Assist, AiDriving, HumanContesting, Emergency };

const char* authority_name(Authority a);

struct TakeoverConfig {
    int contest_window_ticks = 40;     // second gesture must land within this many ticks of the first
    int gesture_release_ticks = 3;     // input must stay quiet this long before a new gesture counts
    double deliberate_stick = 0.5;
    double conflict_angle_deg = 60.0;  // stick heading this far from the AI heading = a contest
    double look_flick_deg_per_tick = 10.0;
    int human_hold_ticks = 100;        // after a human takeover the AI may not re-engage for this long
    bool intent_reengage = true;
    double reengage_confidence = 0.8;
    int emergency_latch_ticks = 10;    // emergency stays latched after the request clears
    bool survival_overrides_human = true;
};

struct AuthorityInput {
    std::uint64_t tick = 0;
    HumanIntentFrame human{};
    PredictedIntent predicted{};
    double player_yaw_deg = 0.0;
    bool ai_has_plan = false;               // the AI has something to drive (fight, bridge, ...)
    std::optional<double> ai_heading_deg;   // world heading the AI is currently commanding
    bool emergency_request = false;         // SurvivalGuardian: death-risk intervention required
};

struct AuthorityDecision {
    Authority state = Authority::Human;
    bool ai_moves = false;
    bool ai_looks = false;
    bool ai_acts = false;                   // AI may issue attack/place/item/utility proposals
    bool emergency = false;
    bool human_took_control = false;        // fired on the tick control moved to the human
    bool contest_registered = false;        // a gesture was counted this tick
    int contest_count = 0;
    std::optional<double> hint_heading_deg; // while contested: the direction the human tried
    std::string reason;
};

class TakeoverAuthority {
public:
    explicit TakeoverAuthority(TakeoverConfig config = {});
    void set_user_mode(UserMode mode);
    UserMode user_mode() const { return user_mode_; }
    AuthorityDecision update(const AuthorityInput& input);
    Authority base_state() const { return base_; }
    void panic_release();  // same as pressing the explicit-override button once

private:
    TakeoverConfig cfg_;
    UserMode user_mode_ = UserMode::Assist;
    Authority base_ = Authority::Assist;
    int quiet_ticks_ = 1000;
    int contest_count_ = 0;
    std::uint64_t contest_start_ = 0;
    std::uint64_t hold_until_ = 0;
    int emergency_hold_ = 0;
    bool contesting(const AuthorityInput& in) const;
};

} // namespace bedrockai
