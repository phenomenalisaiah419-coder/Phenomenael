#include "core/movement_controller.hpp"

#include <algorithm>
#include <cmath>

namespace bedrockai {

MovementController::MovementController(MovementLimits limits) : limits_(limits) {}

double MovementController::wrap_degrees(double degrees) {
    while (degrees > 180.0) degrees -= 360.0;
    while (degrees <= -180.0) degrees += 360.0;
    return degrees;
}

double MovementController::horizontal_distance(const Vec3& a, const Vec3& b) {
    const double dx = b.x - a.x;
    const double dz = b.z - a.z;
    return std::sqrt(dx * dx + dz * dz);
}

double MovementController::clamp(double value, double lo, double hi) {
    return std::max(lo, std::min(hi, value));
}

MovementCommand MovementController::compute(const WorldSnapshot& snapshot, const MovementTarget& target) const {
    MovementCommand out;
    const Vec3 p = snapshot.player.position;
    const double dx = target.position.x - p.x;
    const double dz = target.position.z - p.z;
    const double dy = target.position.y - p.y;
    const double distance = horizontal_distance(p, target.position);

    if (target.style == MovementStyle::Stop || distance <= limits_.arrive_distance) {
        out.valid = true;
        out.sneak = target.allow_sneak;
        return out;
    }

    double desired_x = dx;
    double desired_z = dz;
    if (target.style == MovementStyle::Retreat) {
        desired_x = -dx;
        desired_z = -dz;
    } else if (target.style == MovementStyle::Circle) {
        const double length = std::max(0.001, distance);
        const double nx = dx / length;
        const double nz = dz / length;
        const double tangent_x = -nz * (target.circle_direction >= 0.0 ? 1.0 : -1.0);
        const double tangent_z = nx * (target.circle_direction >= 0.0 ? 1.0 : -1.0);
        const double radial_error = distance - target.circle_radius;
        desired_x = tangent_x + nx * radial_error;
        desired_z = tangent_z + nz * radial_error;
    }

    constexpr double kPi = 3.14159265358979323846;
    const double desired_yaw = std::atan2(-desired_x, desired_z) * 180.0 / kPi;
    const double relative_yaw = wrap_degrees(desired_yaw - snapshot.player.rotation.yaw);
    const double radians = relative_yaw * kPi / 180.0;

    // Decompose the desired world direction into the player's local forward/right axes.
    const double length = std::max(0.001, std::sqrt(desired_x * desired_x + desired_z * desired_z));
    const double world_x = desired_x / length;
    const double world_z = desired_z / length;
    const double yaw_rad = snapshot.player.rotation.yaw * kPi / 180.0;
    const double forward_x = -std::sin(yaw_rad);
    const double forward_z = std::cos(yaw_rad);
    const double right_x = std::cos(yaw_rad);
    const double right_z = std::sin(yaw_rad);

    out.forward = clamp(world_x * forward_x + world_z * forward_z, -limits_.max_forward, limits_.max_forward);
    out.strafe = clamp(world_x * right_x + world_z * right_z, -limits_.max_strafe, limits_.max_strafe);
    out.yaw = relative_yaw;
    out.jump = target.allow_jump && snapshot.player.on_ground && dy > limits_.jump_height_threshold;
    out.sneak = target.allow_sneak;
    out.sprint = target.allow_sprint && distance > 2.0 && std::abs(out.forward) > limits_.sprint_speed_threshold && out.forward > 0.0;
    out.valid = std::isfinite(out.forward) && std::isfinite(out.strafe) && std::isfinite(out.yaw) && std::isfinite(radians);
    return out;
}

} // namespace bedrockai
