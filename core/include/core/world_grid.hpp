#pragma once

// Environment understanding (v0.35).
//
// WorldGrid stores what the client has actually observed about blocks.  Three rules are
// load-bearing for safety:
//   1. Unknown is first-class: a cell that was never observed is Unknown, never Air.
//   2. A cell is inferred Air only when its chunk column is explicitly marked loaded and no
//      explicit block was recorded there.
//   3. The pack() key is collision-free for |x|,|z| < 2^25 and y in [-2048, 2047]
//      (the legacy block_key() in state.hpp is lossy beyond ~4M blocks; see docs/AUDIT_V035.md).

#include "core/state.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace bedrockai {

enum class BlockClass : std::uint8_t {
    Unknown = 0,
    Air,
    Solid,
    Water,
    Lava,
    Hazard,     // solid but harmful: cactus, magma, campfire...
    Web,        // passable, slows, does not support
    Climbable,  // ladder, vine, scaffolding
    Bounce,     // slime-like: cancels fall damage
    Bed,        // protected by policy: never mined / broken / targeted by the AI
    Protected   // server- or user-marked protected block
};

constexpr bool block_is_support(BlockClass c) {
    return c == BlockClass::Solid || c == BlockClass::Hazard || c == BlockClass::Bounce ||
           c == BlockClass::Bed || c == BlockClass::Protected;
}
constexpr bool block_is_passable(BlockClass c) {
    return c == BlockClass::Air || c == BlockClass::Water || c == BlockClass::Lava ||
           c == BlockClass::Web || c == BlockClass::Climbable;
}
constexpr bool block_is_liquid(BlockClass c) { return c == BlockClass::Water || c == BlockClass::Lava; }
constexpr bool block_is_ai_protected(BlockClass c) { return c == BlockClass::Bed || c == BlockClass::Protected; }
const char* block_class_name(BlockClass c);

inline int chunk_coord(int v) { return v >= 0 ? v / 16 : -((-v + 15) / 16); }

class WorldGrid {
public:
    explicit WorldGrid(int min_y = -64, int max_y = 319);

    static std::uint64_t pack(int x, int y, int z);

    void set(int x, int y, int z, BlockClass c);
    void fill(int x0, int y0, int z0, int x1, int y1, int z1, BlockClass c);  // inclusive box
    void erase(int x, int y, int z);
    BlockClass get(int x, int y, int z) const;

    void mark_chunk_loaded(int block_x, int block_z);
    void mark_region_loaded(int x0, int z0, int x1, int z1);
    bool chunk_loaded(int block_x, int block_z) const;

    int min_y() const { return min_y_; }
    int max_y() const { return max_y_; }
    std::size_t explicit_blocks() const { return blocks_.size(); }

    using Classifier = std::function<BlockClass(const BlockObservation&)>;
    // Default classifier: solid -> Solid, everything else stays Unknown (conservative).
    static WorldGrid from_world_state(const WorldState& world, const Classifier& classifier = {},
                                      int min_y = -64, int max_y = 319);

private:
    int min_y_;
    int max_y_;
    std::unordered_map<std::uint64_t, BlockClass> blocks_;
    std::unordered_set<std::uint64_t> loaded_chunks_;
    static std::uint64_t chunk_key(int cx, int cz);
};

} // namespace bedrockai
