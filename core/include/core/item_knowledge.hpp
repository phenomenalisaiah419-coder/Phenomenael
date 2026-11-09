#pragma once

#include "core/projectile_solver.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bedrockai {

enum class ItemCapability : std::uint64_t {
    None = 0,
    MeleeWeapon = 1ULL << 0U,
    RangedWeapon = 1ULL << 1U,
    Food = 1ULL << 2U,
    Healing = 1ULL << 3U,
    Defence = 1ULL << 4U,
    Projectile = 1ULL << 5U,
    Teleport = 1ULL << 6U,
    Knockback = 1ULL << 7U,
    BurstMovement = 1ULL << 8U,
    BlockPlace = 1ULL << 9U,
    ClutchRecovery = 1ULL << 10U,
    MiningTool = 1ULL << 11U,
    FireControl = 1ULL << 12U,
    WebEscape = 1ULL << 13U,
    BridgeUtility = 1ULL << 14U,
    ObjectiveDamage = 1ULL << 15U,
    Drinkable = 1ULL << 16U,
    Throwable = 1ULL << 17U,
    Mobility = 1ULL << 18U,
    ResourceUtility = 1ULL << 19U,
    Explosive = 1ULL << 20U,
    CustomUse = 1ULL << 21U,
    UnsafeConsumable = 1ULL << 22U
};

using ItemCapabilityMask = std::uint64_t;
constexpr ItemCapabilityMask capability_mask(ItemCapability capability) {
    return static_cast<ItemCapabilityMask>(capability);
}
constexpr ItemCapabilityMask operator|(ItemCapability left, ItemCapability right) {
    return capability_mask(left) | capability_mask(right);
}
constexpr bool has_capability(ItemCapabilityMask mask, ItemCapability capability) {
    return (mask & capability_mask(capability)) != 0U;
}

enum class ItemActivationMode : std::uint8_t {
    Unknown,
    Attack,
    Use,
    Consume,
    Throw,
    Place,
    Mine,
    Defend,
    Interact
};

using ItemActivationMask = std::uint32_t;
constexpr ItemActivationMask activation_mask(ItemActivationMode mode) {
    return mode == ItemActivationMode::Unknown ? 0U : (1U << static_cast<unsigned int>(mode));
}
constexpr bool has_activation(ItemActivationMask mask, ItemActivationMode mode) {
    return (mask & activation_mask(mode)) != 0U;
}

struct ItemDescriptor {
    std::string identifier;
    std::string display_name;
    std::vector<std::string> tags;
    std::string variant_key; // exact metadata/component fingerprint if the host can decode it
};

struct ItemKnowledge {
    std::string identifier;
    std::string display_name;
    ItemCapabilityMask capabilities = 0U;
    ItemActivationMode activation = ItemActivationMode::Unknown;
    ItemActivationMask activations = 0U;
    bool verified = false;
    double confidence = 0.0;
    std::uint32_t observed_outcomes = 0U;
    std::string evidence;
    std::string variant_key;
    std::optional<ProjectilePhysics> projectile_physics;
};

// Standard identifiers are mapped from known game semantics. Label-based guesses are
// suggestions only: they never silently authorize an unverified custom item's use.
ItemKnowledge infer_item_knowledge(const ItemDescriptor& descriptor);

class ItemKnowledgeBase {
public:
    bool register_profile(const ItemKnowledge& profile, std::string* error = nullptr);
    ItemKnowledge resolve(const ItemDescriptor& descriptor) const;
    bool record_outcome(const std::string& identifier, bool success, std::string* error = nullptr);
    bool record_outcome(const ItemDescriptor& descriptor, bool success, std::string* error = nullptr);
    bool save_tsv(const std::string& path, std::string* error = nullptr) const;
    bool load_tsv(const std::string& path, std::string* error = nullptr);
    std::size_t size() const;

private:
    std::unordered_map<std::string, ItemKnowledge> profiles_;
};

} // namespace bedrockai
