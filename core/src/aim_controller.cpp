#include "core/aim_controller.hpp"

#include <algorithm>
#include <cmath>

namespace bedrockai {

AimController::AimController(AimLimits limits) : limits_(limits) {}

double AimController::wrap_degrees(double degrees) {
    while (degrees > 180.0) degrees -= 360.0;
    while (degrees <= -180.0) degrees += 360.0;
    return degrees;
}

double AimController::clamp(double value, double lo, double hi) {
    return std::max(lo, std::min(hi, value));
}

AimCommand AimController::compute(const WorldSnapshot& snapshot, const AimTarget& target) const {
    const double lead = target.predict ? std::max(0.0, target.lead_seconds) : 0.0;
    const Vec3 predicted{
        target.position.x + target.velocity.x * lead,
        target.position.y + target.velocity.y * lead + limits_.target_height,
        target.position.z + target.velocity.z * lead
    };

    const double eye_x = snapshot.player.position.x;
    const double eye_y = snapshot.player.position.y + limits_.eye_height;
    const double eye_z = snapshot.player.position.z;
    const double dx = predicted.x - eye_x;
    const double dy = predicted.y - eye_y;
    const double dz = predicted.z - eye_z;
    const double horizontal = std::sqrt(dx * dx + dz * dz);

    AimCommand out;
    constexpr double kPi = 3.14159265358979323846;
    out.desired_yaw = std::atan2(-dx, dz) * 180.0 / kPi;
    out.desired_pitch = -std::atan2(dy, std::max(0.0001, horizontal)) * 180.0 / kPi;
    const double yaw_error = wrap_degrees(out.desired_yaw - snapshot.player.rotation.yaw);
    const double pitch_error = out.desired_pitch - snapshot.player.rotation.pitch;
    out.yaw_delta = clamp(yaw_error, -limits_.max_yaw_step, limits_.max_yaw_step);
    out.pitch_delta = clamp(pitch_error, -limits_.max_pitch_step, limits_.max_pitch_step);
    out.valid = std::isfinite(out.desired_yaw) && std::isfinite(out.desired_pitch)
             && std::isfinite(out.yaw_delta) && std::isfinite(out.pitch_delta);
    return out;
}

} // namespace bedrockai
