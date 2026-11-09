#include "core/takeover_authority.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bedrockai {

const char* authority_name(Authority a) {
    switch (a) {
        case Authority::Human: return "human";
        case Authority::Assist: return "assist";
        case Authority::AiDriving: return "ai_driving";
        case Authority::HumanContesting: return "human_contesting";
        case Authority::Emergency: return "emergency";
    }
    return "human";
}

TakeoverAuthority::TakeoverAuthority(TakeoverConfig config) : cfg_(config) {}

void TakeoverAuthority::set_user_mode(UserMode mode) {
    user_mode_ = mode;
    contest_count_ = 0;
    if (mode == UserMode::Off || mode == UserMode::GuardOnly) {
        base_ = Authority::Human;
        if (mode == UserMode::Off) emergency_hold_ = 0;
    } else {
        base_ = Authority::Assist;
    }
}

void TakeoverAuthority::panic_release() { set_user_mode(UserMode::Off); }

bool TakeoverAuthority::contesting(const AuthorityInput& in) const {
    const double mx = std::isfinite(in.human.move_x) ? in.human.move_x : 0.0;
    const double my = std::isfinite(in.human.move_y) ? in.human.move_y : 0.0;
    const bool stick = std::hypot(mx, my) >= cfg_.deliberate_stick;
    bool conflict = false;
    if (stick) {
        if (!in.ai_heading_deg.has_value()) {
            conflict = true;  // AI is not steering: any deliberate stick is a human statement
        } else {
            const double h = HumanIntentPredictor::stick_heading_deg(mx, my, in.player_yaw_deg);
            conflict = HumanIntentPredictor::angle_diff_deg(h, *in.ai_heading_deg) >= cfg_.conflict_angle_deg;
        }
    }
    const bool flick = in.predicted.look_rate_deg_per_tick >= cfg_.look_flick_deg_per_tick;
    return conflict || flick;
}

AuthorityDecision TakeoverAuthority::update(const AuthorityInput& in) {
    AuthorityDecision d;

    // P0: the explicit override button beats everything, emergency included.
    if (in.human.explicit_override) {
        const bool was_ai = base_ == Authority::AiDriving || base_ == Authority::HumanContesting || emergency_hold_ > 0;
        base_ = Authority::Human;
        contest_count_ = 0;
        emergency_hold_ = 0;
        hold_until_ = in.tick + static_cast<std::uint64_t>(cfg_.human_hold_ticks);
        d.state = Authority::Human;
        d.human_took_control = was_ai;
        d.reason = "explicit human override (P0)";
        return d;
    }
    if (user_mode_ == UserMode::Off) {
        d.state = Authority::Human;
        d.reason = "AI disabled";
        return d;
    }

    // Gesture detection (debounced: a held input is one gesture, not many).
    const bool c = contesting(in);
    bool gesture = false;
    if (c) {
        if (quiet_ticks_ >= cfg_.gesture_release_ticks) gesture = true;
        quiet_ticks_ = 0;
    } else if (quiet_ticks_ < std::numeric_limits<int>::max() - 1) {
        ++quiet_ticks_;
    }

    // Emergency overlay (survival overrides the human).
    const bool guard_allowed = cfg_.survival_overrides_human;
    if (guard_allowed && in.emergency_request) emergency_hold_ = cfg_.emergency_latch_ticks + 1;
    else if (emergency_hold_ > 0) --emergency_hold_;
    const bool emergency = guard_allowed && emergency_hold_ > 0;

    // Base state machine.  Gestures during an emergency are ignored: you cannot argue with survival.
    if (user_mode_ == UserMode::GuardOnly) {
        base_ = Authority::Human;
    } else if (!emergency) {
        if (base_ == Authority::Human) base_ = Authority::Assist;
        switch (base_) {
            case Authority::Assist: {
                if (in.tick >= hold_until_ && in.ai_has_plan) {
                    const bool by_mode = user_mode_ == UserMode::Takeover;
                    const bool by_intent = user_mode_ == UserMode::Assist && cfg_.intent_reengage &&
                                           (in.predicted.kind == IntentKind::AttackHold ||
                                            in.predicted.kind == IntentKind::PlaceHold) &&
                                           in.predicted.confidence >= cfg_.reengage_confidence;
                    if (by_mode || by_intent) {
                        base_ = Authority::AiDriving;
                        contest_count_ = 0;
                        d.reason = by_mode ? "AI engages (takeover mode, plan ready)"
                                           : "AI follows through on predicted human intent";
                    }
                }
                break;
            }
            case Authority::AiDriving: {
                if (!in.ai_has_plan) {
                    base_ = Authority::Assist;
                    d.reason = "AI has no plan";
                } else if (gesture) {
                    contest_count_ = 1;
                    contest_start_ = in.tick;
                    base_ = Authority::HumanContesting;
                    d.contest_registered = true;
                    d.reason = "first deliberate human input: AI keeps control";
                }
                break;
            }
            case Authority::HumanContesting: {
                if (!in.ai_has_plan) {
                    base_ = Authority::Assist;
                    contest_count_ = 0;
                    d.reason = "AI has no plan";
                } else if (in.tick - contest_start_ > static_cast<std::uint64_t>(cfg_.contest_window_ticks)) {
                    base_ = Authority::AiDriving;
                    contest_count_ = 0;
                    d.reason = "contest window expired: AI keeps control";
                    if (gesture) {  // an expired window means this gesture is a fresh first one
                        contest_count_ = 1;
                        contest_start_ = in.tick;
                        base_ = Authority::HumanContesting;
                        d.contest_registered = true;
                        d.reason = "first deliberate human input: AI keeps control";
                    }
                } else if (gesture) {
                    contest_count_ = 2;
                    d.contest_registered = true;
                    base_ = Authority::Assist;
                    hold_until_ = in.tick + static_cast<std::uint64_t>(cfg_.human_hold_ticks);
                    d.human_took_control = true;
                    d.reason = "second deliberate input: human takes control";
                }
                break;
            }
            case Authority::Human:
            case Authority::Emergency:
                break;
        }
    }

    d.contest_count = contest_count_;
    if (emergency) {
        d.state = Authority::Emergency;
        d.emergency = true;
        d.ai_moves = d.ai_looks = d.ai_acts = true;
        if (d.reason.empty()) d.reason = "survival emergency: AI overrides";
        return d;
    }
    d.state = base_;
    if (base_ == Authority::AiDriving || base_ == Authority::HumanContesting) {
        d.ai_moves = d.ai_looks = d.ai_acts = true;
        if (base_ == Authority::HumanContesting && in.predicted.moving)
            d.hint_heading_deg = in.predicted.heading_deg;
    }
    if (d.reason.empty()) d.reason = authority_name(d.state);
    return d;
}

} // namespace bedrockai
