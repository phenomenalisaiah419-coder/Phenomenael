#pragma once

#include "state.hpp"
#include "movement_controller.hpp"

#include <optional>

namespace bedrockai {

enum class BridgeMode { Normal, Combat, Upward, Recovery };

struct BridgeConfig {
    double forward_step = 1.0;
    double max_place_distance = 4.5; // probe value, not assumed Bedrock truth
    int max_forward_blocks = 8;
};

struct BlockPlacementTarget {
    int x = 0;
    int y = 0;
    int z = 0;
    int face = -1;
    bool emergency_allowed = false;
    bool valid = false;
};

struct BridgeCommand {
    BridgeMode mode = BridgeMode::Normal;
    MovementCommand movement{};
    std::optional<BlockPlacementTarget> placement;
    bool requires_place_hold = true;
    bool valid = false;
};

class BridgeController {
public:
    explicit BridgeController(BridgeConfig config = {});
    BridgeCommand compute(const WorldSnapshot& snapshot, const Vec3& destination,
                          BridgeMode mode, bool human_place_held,
                          bool emergency) const;

private:
    BridgeConfig config_;
    static double horizontal_distance(const Vec3& a, const Vec3& b);
    static int floor_int(double v);
};

} // namespace bedrockai
