#pragma once

// Human intent prediction (v0.35).
//
// Turns the raw touch stream (HumanIntentFrame) into a PredictedIntent: what the human is
// doing right now, how deliberately, in which WORLD heading, and how likely they are to keep
// that heading (learned online from their own behaviour).  Pure and deterministic.

#include "core/human_intent.hpp"

#include <cstddef>
#include <cstdint>

namespace bedrockai {

enum class IntentKind : std::uint8_t { Idle, Move, AttackHold, PlaceHold };

struct PredictedIntent {
    IntentKind kind = IntentKind::Idle;
    double confidence = 0.0;
    bool moving = false;
    double heading_deg = 0.0;       // world heading of intended motion (yaw convention), valid when moving
    double stick_magnitude = 0.0;
    double look_rate_deg_per_tick = 0.0;
    bool flick = false;             // camera flick this tick
    bool attack_started = false;    // rising edge
    bool place_started = false;
    bool sprinting = false;
    double keep_heading_probability = 0.5;  // learned P(next tick keeps this heading within tolerance)
    int moving_run_ticks = 0;
    int attack_run_ticks = 0;
    int place_run_ticks = 0;
};

struct IntentPredictorConfig {
    double deadzone = 0.15;
    double flick_deg_per_tick = 10.0;
    double look_to_degrees = 1.0;           // host-unit conversion for look_dx/look_dy
    double keep_heading_tolerance_deg = 30.0;
};

class HumanIntentPredictor {
public:
    explicit HumanIntentPredictor(IntentPredictorConfig config = {});
    void observe(std::uint64_t tick, const HumanIntentFrame& frame, double player_yaw_deg);
    const PredictedIntent& current() const { return current_; }
    void reset();

    // World heading (same yaw convention as MovementController) of a stick deflection.
    static double stick_heading_deg(double move_x, double move_y, double yaw_deg);
    static double angle_diff_deg(double a, double b);  // absolute smallest difference, [0,180]

private:
    IntentPredictorConfig cfg_;
    PredictedIntent current_{};
    bool prev_attack_ = false;
    bool prev_place_ = false;
    bool prev_moving_ = false;
    double prev_heading_ = 0.0;
    std::uint64_t keep_hits_ = 0;
    std::uint64_t keep_total_ = 0;
};

} // namespace bedrockai
