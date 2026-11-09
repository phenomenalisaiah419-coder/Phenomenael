#pragma once

#include "state.hpp"

namespace bedrockai {

struct AimLimits {
    double max_yaw_step = 45.0;
    double max_pitch_step = 45.0;
    double target_height = 0.5;
    double eye_height = 1.62;
};

struct AimTarget {
    Vec3 position{};
    Vec3 velocity{};
    bool predict = true;
    double lead_seconds = 0.0;
};

struct AimCommand {
    double yaw_delta = 0.0;
    double pitch_delta = 0.0;
    double desired_yaw = 0.0;
    double desired_pitch = 0.0;
    bool valid = false;
};

class AimController {
public:
    explicit AimController(AimLimits limits = {});
    AimCommand compute(const WorldSnapshot& snapshot, const AimTarget& target) const;

private:
    AimLimits limits_;
    static double wrap_degrees(double degrees);
    static double clamp(double value, double lo, double hi);
};

} // namespace bedrockai
