#include "core/projectile_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bedrockai {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegreesPerRadian = 180.0 / kPi;
constexpr double kRadiansPerDegree = kPi / 180.0;

bool finite_vec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

double distance(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct Evaluation {
    double miss = std::numeric_limits<double>::infinity();
    double yaw = 0.0;
    double pitch = 0.0;
    double time = 0.0;
};

Evaluation evaluate(const ProjectilePhysics& physics, const ProjectileRequest& request,
                    double flight_time, double pitch) {
    Evaluation result;
    if (flight_time <= 0.0) return result;
    const Vec3 predicted_target{
        request.target_position.x + request.target_velocity.x * (request.latency_seconds + flight_time),
        request.target_position.y + request.target_velocity.y * (request.latency_seconds + flight_time),
        request.target_position.z + request.target_velocity.z * (request.latency_seconds + flight_time)};
    const double dx = predicted_target.x - request.origin.x;
    const double dz = predicted_target.z - request.origin.z;
    const double yaw = std::atan2(-dx, dz) * kDegreesPerRadian;
    const double yaw_rad = yaw * kRadiansPerDegree;
    const double pitch_rad = pitch * kRadiansPerDegree;
    const double horizontal_scale = std::cos(pitch_rad);
    Vec3 velocity{
        -std::sin(yaw_rad) * horizontal_scale * physics.launch_speed,
        -std::sin(pitch_rad) * physics.launch_speed,
        std::cos(yaw_rad) * horizontal_scale * physics.launch_speed};
    Vec3 position = request.origin;
    const double raw_steps = std::ceil(flight_time * physics.tick_rate);
    if (!std::isfinite(raw_steps) || raw_steps < 1.0 || raw_steps > 1000.0) return result;
    const int steps = static_cast<int>(raw_steps);
    const double dt = flight_time / static_cast<double>(steps);
    const double drag_tick = std::pow(physics.drag_per_tick, dt * physics.tick_rate);
    for (int i = 0; i < steps; ++i) {
        position.x += velocity.x * dt;
        position.y += velocity.y * dt;
        position.z += velocity.z * dt;
        velocity.x *= drag_tick;
        velocity.y = velocity.y * drag_tick - physics.gravity * dt;
        velocity.z *= drag_tick;
    }
    result.miss = distance(position, predicted_target);
    result.yaw = yaw;
    result.pitch = pitch;
    result.time = flight_time;
    return result;
}

void keep_better(Evaluation& best, const Evaluation& candidate) {
    if (candidate.miss < best.miss) best = candidate;
}
} // namespace

ProjectileSolution ProjectileSolver::solve(const ProjectilePhysics& physics,
                                            const ProjectileRequest& request) const {
    ProjectileSolution result;
    if (!physics.calibrated || !finite_vec(request.origin) || !finite_vec(request.target_position) ||
        !finite_vec(request.target_velocity) || !std::isfinite(request.latency_seconds) ||
        request.latency_seconds < 0.0 || !std::isfinite(physics.launch_speed) ||
        physics.launch_speed <= 0.0 || !std::isfinite(physics.gravity) || physics.gravity < 0.0 ||
        !std::isfinite(physics.drag_per_tick) || physics.drag_per_tick <= 0.0 ||
        physics.drag_per_tick > 1.0 || !std::isfinite(physics.tick_rate) ||
        physics.tick_rate < 5.0 || physics.tick_rate > 120.0 ||
        !std::isfinite(physics.max_flight_seconds) || physics.max_flight_seconds <= 0.0 ||
        physics.max_flight_seconds > 10.0 || !std::isfinite(physics.hit_tolerance) ||
        physics.hit_tolerance <= 0.0 || physics.hit_tolerance > 4.0) {
        return result;
    }

    const double initial_distance = distance(request.origin, request.target_position);
    if (!std::isfinite(initial_distance) || initial_distance < 0.05 || initial_distance > 512.0) return result;

    Evaluation best;
    // Coarse global search. All physics constants are supplied by a calibrated profile.
    // This is intentionally conservative: no claimed shot is emitted outside tolerance.
    for (double time = 0.05; time <= physics.max_flight_seconds + 1e-9; time += 0.05) {
        for (double pitch = -85.0; pitch <= 85.0; pitch += 1.0) {
            keep_better(best, evaluate(physics, request, time, pitch));
        }
    }
    if (!std::isfinite(best.miss)) return result;

    // Local refinement around the best coarse solution.
    const double coarse_pitch = best.pitch;
    const double coarse_time = best.time;
    Evaluation refined = best;
    for (double time = std::max(0.025, coarse_time - 0.055);
         time <= std::min(physics.max_flight_seconds, coarse_time + 0.055) + 1e-9; time += 0.005) {
        for (double pitch = std::max(-89.0, coarse_pitch - 1.1);
             pitch <= std::min(89.0, coarse_pitch + 1.1) + 1e-9; pitch += 0.1) {
            keep_better(refined, evaluate(physics, request, time, pitch));
        }
    }

    result.yaw_degrees = refined.yaw;
    result.pitch_degrees = refined.pitch;
    result.flight_time_seconds = refined.time;
    result.miss_distance = refined.miss;
    result.valid = std::isfinite(refined.miss) && refined.miss <= physics.hit_tolerance;
    return result;
}

} // namespace bedrockai
