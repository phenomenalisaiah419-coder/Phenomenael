#pragma once

#include "state.hpp"

namespace bedrockai {

struct TouchFrame {
    bool attack = false;
    bool place = false;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
    bool utility = false;
    bool explicit_override = false;
    double move_x = 0;
    double move_y = 0;
    double look_dx = 0;
    double look_dy = 0;
};

struct HumanIntentFrame {
    bool attack_held = false;
    bool place_held = false;
    bool jump_held = false;
    bool sneak_held = false;
    bool sprint_held = false;
    bool utility_held = false;
    bool explicit_override = false;
    double move_x = 0;
    double move_y = 0;
    double look_dx = 0;
    double look_dy = 0;
};

class HumanIntentMapper {
public:
    HumanIntentFrame map(const TouchFrame& touch) const;
};

} // namespace bedrockai
