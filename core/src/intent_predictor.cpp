#include "core/intent_predictor.hpp"

#include <algorithm>
#include <cmath>

namespace bedrockai {
namespace {
constexpr double kPi = 3.14159265358979323846;
double wrap180(double d) {
    while (d > 180.0) d -= 360.0;
    while (d <= -180.0) d += 360.0;
    return d;
}
} // namespace

HumanIntentPredictor::HumanIntentPredictor(IntentPredictorConfig config) : cfg_(config) {}

void HumanIntentPredictor::reset() {
    current_ = PredictedIntent{};
    prev_attack_ = prev_place_ = prev_moving_ = false;
    prev_heading_ = 0.0;
    keep_hits_ = keep_total_ = 0;
}

double HumanIntentPredictor::angle_diff_deg(double a, double b) { return std::fabs(wrap180(a - b)); }

double HumanIntentPredictor::stick_heading_deg(double move_x, double move_y, double yaw_deg) {
    const double yaw = yaw_deg * kPi / 180.0;
    // forward = (-sin yaw, cos yaw); strafe = (cos yaw, sin yaw)  (same convention as MovementController)
    const double wx = move_y * -std::sin(yaw) + move_x * std::cos(yaw);
    const double wz = move_y * std::cos(yaw) + move_x * std::sin(yaw);
    return wrap180(std::atan2(-wx, wz) * 180.0 / kPi);
}

void HumanIntentPredictor::observe(std::uint64_t, const HumanIntentFrame& f, double player_yaw_deg) {
    PredictedIntent next;
    const double mx = std::isfinite(f.move_x) ? f.move_x : 0.0;
    const double my = std::isfinite(f.move_y) ? f.move_y : 0.0;
    next.stick_magnitude = std::min(1.0, std::hypot(mx, my));
    next.moving = next.stick_magnitude > cfg_.deadzone;
    if (next.moving) next.heading_deg = stick_heading_deg(mx, my, player_yaw_deg);
    const double lx = std::isfinite(f.look_dx) ? f.look_dx : 0.0;
    const double ly = std::isfinite(f.look_dy) ? f.look_dy : 0.0;
    next.look_rate_deg_per_tick = std::hypot(lx, ly) * cfg_.look_to_degrees;
    next.flick = next.look_rate_deg_per_tick >= cfg_.flick_deg_per_tick;
    next.sprinting = f.sprint_held;
    next.attack_started = f.attack_held && !prev_attack_;
    next.place_started = f.place_held && !prev_place_;
    next.moving_run_ticks = next.moving ? current_.moving_run_ticks + 1 : 0;
    next.attack_run_ticks = f.attack_held ? current_.attack_run_ticks + 1 : 0;
    next.place_run_ticks = f.place_held ? current_.place_run_ticks + 1 : 0;

    if (next.moving && prev_moving_) {
        ++keep_total_;
        if (angle_diff_deg(next.heading_deg, prev_heading_) <= cfg_.keep_heading_tolerance_deg) ++keep_hits_;
    }
    next.keep_heading_probability =
        (static_cast<double>(keep_hits_) + 1.0) / (static_cast<double>(keep_total_) + 2.0);

    if (f.attack_held) {
        next.kind = IntentKind::AttackHold;
        next.confidence = std::min(0.98, 0.70 + 0.05 * static_cast<double>(next.attack_run_ticks));
    } else if (f.place_held) {
        next.kind = IntentKind::PlaceHold;
        next.confidence = std::min(0.98, 0.70 + 0.05 * static_cast<double>(next.place_run_ticks));
    } else if (next.moving) {
        next.kind = IntentKind::Move;
        next.confidence = std::min(0.99, (0.45 + 0.08 * static_cast<double>(next.moving_run_ticks)) *
                                             (0.5 + 0.5 * next.stick_magnitude));
    } else {
        next.kind = IntentKind::Idle;
        next.confidence = 0.6;
    }
    prev_attack_ = f.attack_held;
    prev_place_ = f.place_held;
    prev_moving_ = next.moving;
    prev_heading_ = next.heading_deg;
    current_ = next;
}

} // namespace bedrockai
