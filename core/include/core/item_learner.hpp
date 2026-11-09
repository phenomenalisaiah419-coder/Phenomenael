#pragma once

// Custom-item learning (v0.35): "learn custom items in servers or modified worlds".
//
// An item is NEVER assumed to do what its name suggests.  Its capabilities are inferred from
// what was actually observed to happen when it was used, either by the human (passive
// observation, always safe) or by the AI inside a tightly gated experiment.
//
//   * Evidence = a trial: which activation was used and what changed (velocity, health,
//     hunger, spawned projectile + its flight samples, target damage/knockback, teleport,
//     placed/removed blocks, item count).
//   * A capability is "learned" only after >= min_trials trials AND hit-rate confidence
//     >= min_confidence (Laplace-smoothed).  Harmful effects are dominant: one trial in which
//     the item hurt the user flags UnsafeConsumable immediately.
//   * Projectile physics are fitted from flight samples and marked calibrated only after
//     several consistent fits.
//   * AI experiments pass through experiment_verdict(): consent, calm surroundings, healthy,
//     safe footing, bounded trial budget, never a consumable unless separately consented.
//   * promote() turns a learned item into a verified ItemKnowledge profile for the existing
//     ItemKnowledgeBase / GameplayBrain, which already treat verified profiles as usable.

#include "core/item_knowledge.hpp"
#include "core/projectile_solver.hpp"
#include "core/state.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bedrockai {

struct TimedPosition {
    std::uint64_t tick = 0;
    Vec3 position{};
};

struct ItemTrial {
    std::string item_key;  // identifier + variant fingerprint; never a display name
    ItemActivationMode activation = ItemActivationMode::Use;
    bool by_human = false;
    Vec3 self_velocity_delta{};
    double self_health_delta = 0.0;
    double hunger_delta = 0.0;
    bool projectile_spawned = false;
    std::vector<TimedPosition> projectile_samples;
    double target_damage = 0.0;
    double target_knockback = 0.0;  // horizontal speed imparted to a target (blocks/tick)
    bool teleported = false;
    Vec3 teleport_delta{};
    bool blocks_placed = false;
    int item_count_delta = 0;
};

struct LearnedItem {
    std::string item_key;
    ItemCapabilityMask capabilities = 0U;
    ItemActivationMask activations = 0U;
    double confidence = 0.0;
    std::uint32_t trials = 0;
    bool unsafe = false;
    double mean_self_health_delta = 0.0;  // e.g. an ender-pearl-like item costs health to use
    std::optional<ProjectilePhysics> projectile;
    std::string evidence;
};

struct ItemLearnerConfig {
    std::uint32_t min_trials = 3;
    double min_confidence = 0.75;
    double velocity_threshold = 0.4;
    double heal_threshold = 1.0;
    double hurt_threshold = 2.0;
    double damage_threshold = 1.0;
    double knockback_threshold = 0.3;
    double teleport_threshold = 3.0;
    std::uint32_t max_ai_trials_per_item = 6;
    std::size_t max_items = 512;
    double tick_rate = 20.0;
};

struct ExperimentContext {
    bool user_allows_experiments = false;
    bool user_allows_consumables = false;
    bool automation_permitted = false;   // from AutomationPolicy
    bool enemies_nearby = false;
    bool emergency = false;
    bool footing_safe = false;           // no hazard within a few blocks
    double health_fraction = 0.0;
    ItemActivationMode activation = ItemActivationMode::Use;
};

struct ExperimentVerdict {
    bool allowed = false;
    std::string reason;
};

class ItemLearner {
public:
    explicit ItemLearner(ItemLearnerConfig config = {});
    void observe(const ItemTrial& trial);
    LearnedItem learned(const std::string& item_key) const;
    // True only for capabilities that are learned AND safe to rely on.
    bool usable_for(const std::string& item_key, ItemCapability capability) const;
    ExperimentVerdict experiment_verdict(const std::string& item_key, const ExperimentContext& ctx) const;
    // Publishes a learned, non-unsafe item into the knowledge base as a verified profile.
    bool promote(const std::string& item_key, const ItemDescriptor& descriptor, ItemKnowledgeBase& base,
                 std::string* error = nullptr) const;
    std::size_t tracked() const { return items_.size(); }

private:
    struct Record {
        std::uint32_t trials = 0;
        std::uint32_t ai_trials = 0;
        std::unordered_map<std::uint64_t, std::uint32_t> hits;  // capability bit -> trials showing it
        ItemActivationMask activations = 0U;
        std::vector<ProjectilePhysics> fits;
        bool unsafe = false;
        double health_sum = 0.0;
    };
    ItemLearnerConfig cfg_;
    std::unordered_map<std::string, Record> items_;
    std::optional<ProjectilePhysics> fit_projectile(const std::vector<TimedPosition>& samples) const;
    std::optional<ProjectilePhysics> consensus(const std::vector<ProjectilePhysics>& fits) const;
};

} // namespace bedrockai
