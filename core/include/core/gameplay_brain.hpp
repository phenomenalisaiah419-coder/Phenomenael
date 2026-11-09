#pragma once

#include "core/combat_controller.hpp"
#include "core/projectile_solver.hpp"
#include "core/item_knowledge.hpp"
#include "core/server_rules.hpp"
#include "core/state.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bedrockai {

enum class GameMode : std::uint8_t { Unknown, Survival, SMP, BedWars, Duel, GeneralPvP, MacePvP, BridgeFight };
enum class GameplayGoal : std::uint8_t { Survive, Fight, Escape, Bridge, Mine, Explore, BedWarsRush, BedWarsDefend, SMPTravel };

enum class ItemKind : std::uint8_t {
    Unknown, Sword, Axe, Mace, Bow, Crossbow, Shield, Food, GoldenApple,
    EnchantedGoldenApple, HealingPotion, SplashHealingPotion, StrengthPotion,
    SpeedPotion, ResistancePotion, EnderPearl, WindCharge, Fireball, WaterBucket,
    MilkBucket, BuildingBlock, Pickaxe, WebTool, Totem, Elytra, FireworkRocket,
    Trident, Arrow, Count
};

struct InventoryStackObservation {
    ItemKind kind = ItemKind::Unknown;
    int slot = -1; // zero-based container slot; current quick-use actions accept hotbar slots 0-8
    int count = 0;
    double hunger_restore = 0.0;
    double saturation_restore = 0.0;
    double durability_fraction = 1.0;
    bool equipped = false;
    bool ready = true;
    // Dynamic item identity/properties preserve server-specific items (including Hive items).
    std::string identifier;
    std::string display_name;
    std::vector<std::string> item_tags;
    std::string variant_key; // stable server-observed variant identity, including relevant metadata/components
    ItemCapabilityMask capabilities = 0U;
    ItemActivationMode activation = ItemActivationMode::Unknown;
    ItemActivationMask activations = 0U;
    bool capability_profile_verified = false;
    double profile_confidence = 0.0;
    std::optional<ProjectilePhysics> custom_projectile_physics;
    // Protocol-2193 NetworkItemStackDescriptor fields. These remain raw until the
    // session's item registry and item-user-data codecs can provide semantic identity.
    std::int16_t protocol_item_id = 0;
    bool protocol_item_id_known = false;
    std::uint32_t protocol_aux_value = 0;
    std::uint32_t protocol_block_runtime_id = 0;
    std::int32_t protocol_network_stack_id = 0;
    bool protocol_network_stack_id_known = false;
    std::string protocol_user_data_buffer; // binary string; may contain NUL bytes
    std::uint8_t protocol_container_id = 255U;
    std::uint8_t protocol_container_name = 255U;
    std::uint32_t protocol_container_dynamic_id = 0U;
    bool protocol_descriptor_decoded = false;
    std::string protocol_registry_identifier;
    bool protocol_registry_resolution_known = false;
    bool protocol_registry_kind_applied = false;
    ItemKind protocol_registry_kind = ItemKind::Unknown;
};

struct ItemRegistryEntryObservation {
    std::int16_t item_id = 0;
    std::string identifier;
    bool component_based = false;
    std::int32_t version = 0;
    std::string component_data_buffer; // exact encoded network NBT bytes when component-based
};

struct InventoryContainerObservation {
    std::uint8_t container_id = 255U;
    std::uint8_t container_name = 255U;
    std::uint32_t dynamic_id = 0U;
    std::vector<InventoryStackObservation> slots;
    std::optional<InventoryStackObservation> storage_item;
};

struct GameplayConditions {
    double maximum_health = 20.0;
    double absorption = 0.0;
    double hunger = 20.0;
    double armour_points = 0.0;
    double armour_durability_fraction = 1.0;
    bool hunger_known = false;
    bool burning = false;
    bool in_web = false;
    bool enemy_attacking = false;
    bool immediate_threat = false;
    bool line_of_sight_known = false;
    bool line_of_sight = false;
    bool safe_heal_window = false;
    bool near_void_edge = false;
    bool falling_toward_void = false;
    bool safe_pearl_landing_known = false;
    Vec3 safe_pearl_landing{};
    bool wind_charge_recovery_calibrated = false;
    bool water_clutch_site_known = false;
    bool water_placement_allowed = false;
    bool block_clutch_site_known = false;
    bool block_placement_allowed = false;
    bool in_nether = false;
    bool mace_smash_window_known = false;
    bool mace_smash_window_open = false;
    bool wind_charge_knockback_window_known = false;
    bool wind_charge_knockback_window_open = false;
    bool fireball_use_supported = false;
    bool target_using_shield_known = false;
    bool target_using_shield = false;
    bool server_axe_shield_punish_supported = false;
    bool splash_potion_use_safe = false;
    bool objective_target_known = false;
    Vec3 objective_target{};
    bool objective_block_known = false;
    int objective_block_x = 0;
    int objective_block_y = 0;
    int objective_block_z = 0;
};

struct GameplayCalibration {
    std::optional<double> melee_reach;
    std::optional<double> melee_interval_seconds;
    ProjectilePhysics arrow{};
    ProjectilePhysics pearl{};
    ProjectilePhysics wind_charge{};
    ProjectilePhysics potion{};
    ProjectilePhysics fireball{};
};

struct VerifiedItemActionOutcome {
    ItemDescriptor item{};
    bool success = false;
};

struct GameplayObservation {
    WorldSnapshot world{};
    GameMode game_mode = GameMode::Unknown;
    GameplayGoal goal = GameplayGoal::Survive;
    std::vector<InventoryStackObservation> inventory;
    // Full protocol inventory snapshots are retained separately so chests/other containers
    // can never be mistaken for the player's hotbar by combat-selection code.
    std::vector<InventoryContainerObservation> inventory_containers;
    std::vector<InventoryStackObservation> armor_inventory; // protocol order: head, torso, legs, feet, body
    std::vector<ItemRegistryEntryObservation> item_registry_entries;
    std::unordered_map<std::int16_t, std::size_t> item_registry_index_by_id; // only unambiguous IDs
    std::unordered_set<std::int16_t> ambiguous_protocol_item_ids;
    std::optional<InventoryStackObservation> held_item_observation;
    // Stable host-supplied key, e.g. canonical server endpoint plus game mode. Learning is scoped to it.
    std::string server_profile_key;
    ServerMechanicsProfile server_mechanics{};
    GameplayConditions conditions{};
    GameplayCalibration calibration{};
    double latency_ms = 0.0;
    std::optional<std::uint64_t> selected_target;
    // Set only when the source can confirm a complete, controlled attack trial. The loop
    // will not infer Java/Bedrock cadence from a client label or an unverified health change.
    std::optional<AttackTrial> verified_attack_trial;
    // Exact item variant + observed server-side outcome; can update a registered profile.
    std::optional<VerifiedItemActionOutcome> verified_item_outcome;
};

enum class SkillId : std::uint8_t {
    ObserveAndWait,
    ChooseMeleeStyle,
    SwordStrafeCombo,
    AxePunish,
    ShieldDefence,
    BowLeadShot,
    CrossbowShot,
    MaceSmash,
    WindChargeKnockback,
    WindChargeClutch,
    PearlEscape,
    PearlChase,
    WaterBucketClutch,
    BlockClutch,
    EatFood,
    GoldenAppleHeal,
    DrinkHealingPotion,
    SplashHealingPotion,
    ExtinguishFire,
    EscapeWeb,
    SafeBridge,
    SpeedBridge,
    DiagonalBridge,
    StaircaseBridge,
    MineObjective,
    ResourceGathering,
    BedWarsRush,
    BedWarsDefend,
    SMPTravel,
    DodgeProjectile,
    SprintJump,
    CircleStrafe,
    RetreatAndReset,
    SwitchToBestWeapon,
    EmergencyReposition,
    Count
};

enum class SkillFamily : std::uint8_t { Perception, Combat, Movement, Ranged, Utility, Survival, Building, Resource, Planning };

enum class BrainAction : std::uint8_t {
    Wait, Observe, Approach, Attack, Strafe, Retreat, AimAndShootBow, AimAndShootCrossbow,
    UseMace, UseWindCharge, UseFireball, ThrowPearl, EatFood, EatGoldenApple, DrinkHealingPotion,
    ThrowHealingPotion, RaiseShield, ExtinguishFire, EscapeWeb, WaterClutch, BlockClutch,
    Bridge, MineBlock, Explore, SwitchWeapon, Reposition, NoSafeRecoveryAction, UseAdaptiveItem
};

struct SkillDefinition {
    SkillId id = SkillId::ObserveAndWait;
    const char* name = "observe_and_wait";
    SkillFamily family = SkillFamily::Perception;
    const char* description = "Observe game state and wait for a supported decision.";
    bool requires_calibration = false;
    bool can_learn_from_outcomes = true;
};

const std::vector<SkillDefinition>& gameplay_skill_catalog();

struct SkillExperienceRecord {
    std::uint64_t attempts = 0U;
    std::uint64_t successes = 0U;
    double mean_reward = 0.5;
};

// Outcome memory is intentionally independent of account credentials and game-session secrets.
// A host can persist it in the user's private Termux data directory.
class SkillExperienceBook {
public:
    void record(SkillId id, bool success, double reward = 1.0);
    SkillExperienceRecord get(SkillId id) const;
    double effectiveness(SkillId id) const;
    bool save_tsv(const std::string& path, std::string* error = nullptr) const;
    bool load_tsv(const std::string& path, std::string* error = nullptr);
private:
    std::array<SkillExperienceRecord, static_cast<std::size_t>(SkillId::Count)> records_{};
};

struct GameplayBrainConfig {
    double critical_health_fraction = 0.25;
    double retreat_health_fraction = 0.40;
    double heal_health_fraction = 0.68;
    double eat_hunger_threshold = 14.0;
    double finish_target_health = 6.0;
    double projectile_min_distance = 4.0;
    double target_eye_height = 1.0;
};

struct GameplayDecision {
    BrainAction action = BrainAction::Observe;
    SkillId skill = SkillId::ObserveAndWait;
    CombatStyle combat_style = CombatStyle::Aggressive;
    std::optional<std::uint64_t> target_runtime_id;
    std::optional<Vec3> target_position;
    std::optional<int> item_slot;
    std::optional<std::string> item_identifier;
    ItemActivationMode item_activation = ItemActivationMode::Unknown;
    std::optional<ProjectileSolution> projectile;
    CombatCommand combat{};
    MovementCommand movement{};
    AimCommand aim{};
    double confidence = 0.0;
    bool emergency = false;
    bool valid = false;
    std::string reason;
};

class GameplayBrain {
public:
    explicit GameplayBrain(GameplayBrainConfig config = {});
    GameplayDecision decide(const GameplayObservation& observation,
                            const SkillExperienceBook* experience = nullptr) const;
private:
    GameplayBrainConfig config_;
};

} // namespace bedrockai
