#include "core/bridge_controller.hpp"

#include <cmath>
#include <algorithm>

namespace bedrockai {
BridgeController::BridgeController(BridgeConfig config) : config_(config) {}

double BridgeController::horizontal_distance(const Vec3& a, const Vec3& b) {
    const double dx = b.x - a.x;
    const double dz = b.z - a.z;
    return std::sqrt(dx * dx + dz * dz);
}

int BridgeController::floor_int(double v) { return static_cast<int>(std::floor(v)); }

BridgeCommand BridgeController::compute(const WorldSnapshot& snapshot, const Vec3& destination,
                                        BridgeMode mode, bool human_place_held,
                                        bool emergency) const {
    BridgeCommand out;
    out.mode = mode;
    MovementTarget target{destination, MovementStyle::Approach, true, true,
                          mode == BridgeMode::Combat, 2.0, 1.0};
    out.movement = MovementController{}.compute(snapshot, target);

    // This controller proposes a location only. The arbiter remains the sole authority
    // over whether the actual place channel may fire.
    const double dist = horizontal_distance(snapshot.player.position, destination);
    if (dist <= static_cast<double>(config_.max_forward_blocks)) {
        const double dx = destination.x - snapshot.player.position.x;
        const double dz = destination.z - snapshot.player.position.z;
        const double len = std::max(0.001, std::sqrt(dx * dx + dz * dz));
        const int px = floor_int(snapshot.player.position.x + dx / len);
        const int pz = floor_int(snapshot.player.position.z + dz / len);
        const int py = floor_int(snapshot.player.position.y - 1.0);

        const auto key = block_key(px, py, pz);
        const auto it = snapshot.world.blocks.find(key);
        const bool occupied = it != snapshot.world.blocks.end() && it->second.solid;
        if (!occupied && (human_place_held || emergency)) {
            BlockPlacementTarget p;
            p.x = px;
            p.y = py;
            p.z = pz;
            // Face semantics are adapter-specific; -1 means not yet validated by the probe.
            p.face = -1;
            p.emergency_allowed = emergency;
            p.valid = true;
            out.placement = p;
        }
    }

    out.valid = out.movement.valid;
    return out;
}
}
