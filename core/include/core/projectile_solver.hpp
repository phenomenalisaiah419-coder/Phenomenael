#pragma once

#include "state.hpp"

namespace bedrockai {

// Physics values must be calibrated for the relevant Bedrock item/version/server.
// An uncalibrated profile is rejected rather than silently using Java-edition data.
struct ProjectilePhysics {
    double launch_speed = 0.0;       // blocks per second
    double gravity = 0.0;            // blocks per second squared; positive downward
    double drag_per_tick = 1.0;      // velocity multiplier per simulation tick
    double tick_rate = 20.0;
    double max_flight_seconds = 4.0;
    double hit_tolerance = 0.6;      // acceptable closest miss distance in blocks
    bool calibrated = false;
};

struct ProjectileRequest {
    Vec3 origin{};
    Vec3 target_position{};
    Vec3 target_velocity{};
    double latency_seconds = 0.0;
};

struct ProjectileSolution {
    double yaw_degrees = 0.0;
    double pitch_degrees = 0.0;      // Minecraft convention: negative points upward
    double flight_time_seconds = 0.0;
    double miss_distance = 0.0;
    bool valid = false;
};

class ProjectileSolver {
public:
    ProjectileSolution solve(const ProjectilePhysics& physics,
                             const ProjectileRequest& request) const;
};

} // namespace bedrockai
