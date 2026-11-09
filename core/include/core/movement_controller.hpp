#pragma once

#include "state.hpp"

#include <cstdint>

namespace bedrockai {

enum class MovementStyle { Approach, Retreat, Strafe, Circle, Stop, Recover };

struct MovementLimits {
    double sprint_speed_threshold = 0.10;
    double arrive_distance = 0.20;
    double jump_height_threshold = 0.65;
    double max_forward = 1.0;
    double max_strafe = 1.0;
};

struct MovementTarget {
    Vec3 position{};
    MovementStyle style = MovementStyle::Approach;
    bool allow_jump = true;
    bool allow_sprint = true;
    bool allow_sneak = false;
    double circle_radius = 2.5;
    double circle_direction = 1.0;
};

struct MovementCommand {
    double forward = 0.0;
    double strafe = 0.0;
    double yaw = 0.0;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
    bool valid = false;
};

class MovementController {
public:
    explicit MovementController(MovementLimits limits = {});

    MovementCommand compute(const WorldSnapshot& snapshot, const MovementTarget& target) const;

private:
    MovementLimits limits_;
    static double wrap_degrees(double degrees);
    static double horizontal_distance(const Vec3& a, const Vec3& b);
    static double clamp(double value, double lo, double hi);
};

} // namespace bedrockai
