#include "core/world_grid.hpp"

#include <algorithm>

namespace bedrockai {

const char* block_class_name(BlockClass c) {
    switch (c) {
        case BlockClass::Unknown: return "unknown";
        case BlockClass::Air: return "air";
        case BlockClass::Solid: return "solid";
        case BlockClass::Water: return "water";
        case BlockClass::Lava: return "lava";
        case BlockClass::Hazard: return "hazard";
        case BlockClass::Web: return "web";
        case BlockClass::Climbable: return "climbable";
        case BlockClass::Bounce: return "bounce";
        case BlockClass::Bed: return "bed";
        case BlockClass::Protected: return "protected";
    }
    return "unknown";
}

WorldGrid::WorldGrid(int min_y, int max_y) : min_y_(min_y), max_y_(max_y) {}

std::uint64_t WorldGrid::pack(int x, int y, int z) {
    const std::uint64_t ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x) & 0x3FFFFFFU);
    const std::uint64_t uz = static_cast<std::uint64_t>(static_cast<std::uint32_t>(z) & 0x3FFFFFFU);
    const std::uint64_t uy = static_cast<std::uint64_t>(static_cast<std::uint32_t>(y) & 0xFFFU);
    return (ux << 38U) | (uz << 12U) | uy;
}

std::uint64_t WorldGrid::chunk_key(int cx, int cz) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32U) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(cz));
}

void WorldGrid::set(int x, int y, int z, BlockClass c) {
    if (c == BlockClass::Unknown) {
        blocks_.erase(pack(x, y, z));
        return;
    }
    blocks_[pack(x, y, z)] = c;
}

void WorldGrid::fill(int x0, int y0, int z0, int x1, int y1, int z1, BlockClass c) {
    for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x)
        for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
            for (int z = std::min(z0, z1); z <= std::max(z0, z1); ++z) set(x, y, z, c);
}

void WorldGrid::erase(int x, int y, int z) { blocks_.erase(pack(x, y, z)); }

BlockClass WorldGrid::get(int x, int y, int z) const {
    const auto it = blocks_.find(pack(x, y, z));
    if (it != blocks_.end()) return it->second;
    return chunk_loaded(x, z) ? BlockClass::Air : BlockClass::Unknown;
}

void WorldGrid::mark_chunk_loaded(int block_x, int block_z) {
    loaded_chunks_.insert(chunk_key(chunk_coord(block_x), chunk_coord(block_z)));
}

void WorldGrid::mark_region_loaded(int x0, int z0, int x1, int z1) {
    for (int cx = chunk_coord(std::min(x0, x1)); cx <= chunk_coord(std::max(x0, x1)); ++cx)
        for (int cz = chunk_coord(std::min(z0, z1)); cz <= chunk_coord(std::max(z0, z1)); ++cz)
            loaded_chunks_.insert(chunk_key(cx, cz));
}

bool WorldGrid::chunk_loaded(int block_x, int block_z) const {
    return loaded_chunks_.count(chunk_key(chunk_coord(block_x), chunk_coord(block_z))) != 0U;
}

WorldGrid WorldGrid::from_world_state(const WorldState& world, const Classifier& classifier, int min_y,
                                      int max_y) {
    WorldGrid grid(min_y, max_y);
    for (const auto& entry : world.blocks) {
        const BlockObservation& b = entry.second;
        BlockClass c = BlockClass::Unknown;
        if (classifier) c = classifier(b);
        else if (b.solid) c = BlockClass::Solid;
        if (c != BlockClass::Unknown) grid.set(b.x, b.y, b.z, c);
    }
    return grid;
}

} // namespace bedrockai
