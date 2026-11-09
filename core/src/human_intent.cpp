#include "core/human_intent.hpp"
namespace bedrockai {
HumanIntentFrame HumanIntentMapper::map(const TouchFrame& t) const {
    HumanIntentFrame out;
    out.attack_held=t.attack; out.place_held=t.place; out.jump_held=t.jump;
    out.sneak_held=t.sneak; out.sprint_held=t.sprint; out.utility_held=t.utility;
    out.explicit_override=t.explicit_override;
    out.move_x=t.move_x; out.move_y=t.move_y; out.look_dx=t.look_dx; out.look_dy=t.look_dy;
    return out;
}
}
