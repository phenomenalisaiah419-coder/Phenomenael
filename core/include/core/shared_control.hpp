#pragma once

#include "arbiter.hpp"
#include "aim_controller.hpp"
#include "combat_controller.hpp"
#include "movement_controller.hpp"
#include "state.hpp"

namespace bedrockai {

struct SharedControlInput {
    HumanInput human{};
    CombatCommand ai{};
};

struct SharedControlOutput {
    ControlFrame channels{};
    MovementCommand movement{};
    AimCommand aim{};
};

class SharedControlController {
public:
    SharedControlOutput compose(const WorldSnapshot& snapshot,
                                 const InventoryState& inventory,
                                 const SharedControlInput& input,
                                 const CapabilityProfile& capabilities) const;
};

} // namespace bedrockai
