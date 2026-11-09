#include "core/gameplay_brain.hpp"

#include "core/bridge_controller.hpp"
#include "core/movement_controller.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>

namespace bedrockai {
namespace {
constexpr std::size_t skill_index(SkillId id) {
    return static_cast<std::size_t>(id);
}

bool finite_vec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

double distance_3d(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double clamp_value(double value, double lo, double hi) {
    return std::max(lo, std::min(hi, value));
}

const InventoryStackObservation* find_item(const GameplayObservation& observation, ItemKind kind) {
    const InventoryStackObservation* best = nullptr;
    for (const auto& stack : observation.inventory) {
        if (stack.kind != kind || stack.count <= 0 || !stack.ready) continue;
        if (stack.slot < 0 || stack.slot > 8) continue;
        if (!std::isfinite(stack.durability_fraction) || stack.durability_fraction < 0.05 ||
            stack.durability_fraction > 1.0) continue;
        if (best == nullptr || (stack.equipped && !best->equipped) ||
            (stack.equipped == best->equipped && stack.count > best->count)) {
            best = &stack;
        }
    }
    return best;
}

bool has_item(const GameplayObservation& observation, ItemKind kind) {
    return find_item(observation, kind) != nullptr;
}

ItemCapabilityMask inferred_known_kind_capabilities(ItemKind kind) {
    switch (kind) {
        case ItemKind::Sword: case ItemKind::Axe: case ItemKind::Mace: case ItemKind::Trident:
            return capability_mask(ItemCapability::MeleeWeapon);
        case ItemKind::Bow: case ItemKind::Crossbow:
            return capability_mask(ItemCapability::RangedWeapon) | capability_mask(ItemCapability::Projectile);
        case ItemKind::Shield:
            return capability_mask(ItemCapability::Defence);
        case ItemKind::Food: case ItemKind::GoldenApple: case ItemKind::EnchantedGoldenApple:
            return capability_mask(ItemCapability::Food) | capability_mask(ItemCapability::Healing);
        case ItemKind::HealingPotion: case ItemKind::SplashHealingPotion:
            return capability_mask(ItemCapability::Healing);
        case ItemKind::EnderPearl:
            return capability_mask(ItemCapability::Teleport) | capability_mask(ItemCapability::ClutchRecovery);
        case ItemKind::WindCharge:
            return capability_mask(ItemCapability::Knockback) | capability_mask(ItemCapability::BurstMovement) |
                   capability_mask(ItemCapability::ClutchRecovery);
        case ItemKind::Fireball:
            return capability_mask(ItemCapability::Explosive) | capability_mask(ItemCapability::Projectile);
        case ItemKind::WaterBucket:
            return capability_mask(ItemCapability::FireControl) | capability_mask(ItemCapability::ClutchRecovery);
        case ItemKind::BuildingBlock:
            return capability_mask(ItemCapability::BlockPlace) | capability_mask(ItemCapability::BridgeUtility);
        case ItemKind::Pickaxe:
            return capability_mask(ItemCapability::MiningTool);
        case ItemKind::WebTool:
            return capability_mask(ItemCapability::WebEscape);
        case ItemKind::Elytra: case ItemKind::FireworkRocket:
            return capability_mask(ItemCapability::Mobility);
        case ItemKind::Unknown: case ItemKind::MilkBucket: case ItemKind::Arrow: case ItemKind::Totem:
        case ItemKind::SpeedPotion: case ItemKind::ResistancePotion: case ItemKind::StrengthPotion:
        case ItemKind::Count:
            return 0U;
    }
    return 0U;
}

ItemCapabilityMask effective_capabilities(const InventoryStackObservation& stack) {
    return stack.capabilities != 0U ? stack.capabilities : inferred_known_kind_capabilities(stack.kind);
}

bool supports_action(const InventoryStackObservation& stack, ItemActivationMode mode) {
    if (stack.activations != 0U) return has_activation(stack.activations, mode);
    return stack.activation == mode;
}

ItemActivationMode preferred_action(const InventoryStackObservation& stack,
                                    std::initializer_list<ItemActivationMode> preferences) {
    for (const ItemActivationMode mode : preferences) if (supports_action(stack, mode)) return mode;
    return ItemActivationMode::Unknown;
}

bool profile_is_usable(const InventoryStackObservation& stack) {
    return stack.kind != ItemKind::Unknown ||
           (stack.capability_profile_verified &&
            (stack.activation != ItemActivationMode::Unknown || stack.activations != 0U) &&
            std::isfinite(stack.profile_confidence) && stack.profile_confidence >= 0.5);
}

const InventoryStackObservation* find_dynamic_item(const GameplayObservation& observation,
                                                   ItemCapability capability) {
    const InventoryStackObservation* best = nullptr;
    for (const auto& stack : observation.inventory) {
        if (stack.kind != ItemKind::Unknown || stack.count <= 0 || !stack.ready || stack.slot < 0 || stack.slot > 8 ||
            !stack.capability_profile_verified ||
            (stack.activation == ItemActivationMode::Unknown && stack.activations == 0U) ||
            !has_capability(effective_capabilities(stack), capability) ||
            !std::isfinite(stack.profile_confidence) || stack.profile_confidence < 0.5 ||
            !std::isfinite(stack.durability_fraction) || stack.durability_fraction < 0.05 || stack.durability_fraction > 1.0) {
            continue;
        }
        const double score = stack.profile_confidence + std::min(0.1, static_cast<double>(stack.count) * 0.001);
        const double best_score = best == nullptr ? -1.0 : best->profile_confidence + std::min(0.1, static_cast<double>(best->count) * 0.001);
        if (best == nullptr || score > best_score) best = &stack;
    }
    return best;
}

std::string item_identity(const InventoryStackObservation& stack) {
    return !stack.identifier.empty() ? stack.identifier : stack.display_name;
}

const InventoryStackObservation* find_custom_melee_weapon(const GameplayObservation& observation) {
    const InventoryStackObservation* best = nullptr;
    double best_score = -1.0;
    for (const auto& stack : observation.inventory) {
        if (stack.kind != ItemKind::Unknown || stack.count <= 0 || !stack.ready || stack.slot < 0 || stack.slot > 8 ||
            !stack.capability_profile_verified || !supports_action(stack, ItemActivationMode::Attack) ||
            !has_capability(effective_capabilities(stack), ItemCapability::MeleeWeapon) ||
            !std::isfinite(stack.profile_confidence) || stack.profile_confidence < 0.5) continue;
        double score = 1.0 + stack.profile_confidence;
        const ItemCapabilityMask caps = effective_capabilities(stack);
        if (has_capability(caps, ItemCapability::Knockback)) score += 0.25;
        if (has_capability(caps, ItemCapability::ObjectiveDamage)) score += 0.25;
        if (has_capability(caps, ItemCapability::Defence)) score += 0.10;
        if (score > best_score) { best_score = score; best = &stack; }
    }
    return best;
}

CombatStyle adaptive_combat_style(const GameplayObservation& observation,
                                  const SkillExperienceBook* experience,
                                  double health_fraction,
                                  double target_distance,
                                  int enemies,
                                  bool target_is_weak) {
    const bool in_range = observation.calibration.melee_reach.has_value() &&
        std::isfinite(*observation.calibration.melee_reach) && target_distance <= *observation.calibration.melee_reach;
    auto learned = [experience](SkillId id) {
        return experience == nullptr ? 0.5 : experience->effectiveness(id);
    };
    double aggressive = 0.60 + (learned(SkillId::ChooseMeleeStyle) - 0.5) * 1.3;
    double strafing = 0.66 + (learned(SkillId::SwordStrafeCombo) - 0.5) * 1.7 +
                      (learned(SkillId::CircleStrafe) - 0.5) * 0.5;
    double defensive = 0.25 + (1.0 - health_fraction) * 1.7 +
                       (learned(SkillId::RetreatAndReset) - 0.5) * 1.3;
    double chase = (in_range ? 0.10 : 1.05) + (learned(SkillId::SprintJump) - 0.5) * 0.8;
    double finish = target_is_weak ? 1.25 + (learned(SkillId::ChooseMeleeStyle) - 0.5) * 0.8 : -1.0;
    double multi_target = enemies > 1 ? 0.45 + 0.20 * static_cast<double>(std::min(enemies, 4)) : -0.5;
    if (observation.game_mode == GameMode::GeneralPvP || observation.game_mode == GameMode::Duel)
        strafing += 0.16;
    if (observation.game_mode == GameMode::BedWars) aggressive += 0.12;
    if (observation.game_mode == GameMode::MacePvP && observation.conditions.mace_smash_window_open &&
        observation.conditions.mace_smash_window_known) aggressive += 0.10;
    if (health_fraction <= 0.20 || observation.conditions.immediate_threat) defensive += 1.0;
    if (enemies >= 2) defensive += 0.55;
    if (observation.conditions.near_void_edge) return CombatStyle::EdgeVoid;
    if (!in_range) chase += 0.30;
    const double highest = std::max({aggressive, strafing, defensive, chase, finish, multi_target});
    if (highest == defensive) return CombatStyle::Defensive;
    if (highest == chase) return CombatStyle::Chase;
    if (highest == finish) return CombatStyle::Finish;
    if (highest == multi_target) return CombatStyle::MultiTarget;
    if (highest == aggressive) return CombatStyle::Aggressive;
    return CombatStyle::Strafing;
}

bool has_ammunition(const GameplayObservation& observation, ItemKind kind) {
    // Ammunition may remain in the main inventory; a wielded/usable item must be hotbar-selectable.
    for (const auto& stack : observation.inventory) {
        if (stack.kind == kind && stack.count > 0 && stack.ready) return true;
    }
    return false;
}

std::optional<std::uint64_t> select_target(const GameplayObservation& observation) {
    if (observation.selected_target.has_value()) {
        const auto it = observation.world.world.entities.find(*observation.selected_target);
        if (it != observation.world.world.entities.end() && it->second.alive && it->second.hostile) {
            return it->first;
        }
    }
    const EntityState* best = nullptr;
    double best_score = -std::numeric_limits<double>::infinity();
    for (const auto& pair : observation.world.world.entities) {
        const EntityState& entity = pair.second;
        if (!entity.alive || !entity.hostile || !finite_vec(entity.position) || !finite_vec(entity.velocity)) continue;
        const double d = distance_3d(observation.world.player.position, entity.position);
        if (!std::isfinite(d)) continue;
        const double health_bonus = entity.health > 0.0 ? clamp_value(20.0 - entity.health, 0.0, 20.0) * 0.10 : 0.0;
        const double score = 100.0 - d * 4.0 + health_bonus;
        if (score > best_score) {
            best_score = score;
            best = &entity;
        }
    }
    if (best == nullptr) return std::nullopt;
    return best->runtime_id;
}

const EntityState* entity_by_id(const WorldSnapshot& world, std::uint64_t id) {
    const auto it = world.world.entities.find(id);
    if (it == world.world.entities.end()) return nullptr;
    return &it->second;
}

int hostile_count(const WorldSnapshot& world) {
    int count = 0;
    for (const auto& pair : world.world.entities) {
        if (pair.second.alive && pair.second.hostile) ++count;
    }
    return count;
}

const InventoryStackObservation* best_food(const GameplayObservation& observation) {
    const InventoryStackObservation* best = nullptr;
    double best_score = -1.0;
    for (const auto& stack : observation.inventory) {
        const bool known_food = stack.kind == ItemKind::Food;
        const bool profiled_food = stack.kind == ItemKind::Unknown && profile_is_usable(stack) &&
            has_capability(effective_capabilities(stack), ItemCapability::Food) &&
            supports_action(stack, ItemActivationMode::Consume);
        if ((!known_food && !profiled_food) || stack.count <= 0 || !stack.ready || stack.slot < 0 || stack.slot > 8) continue;
        const bool unsafe_food = has_capability(effective_capabilities(stack), ItemCapability::UnsafeConsumable);
        if (unsafe_food && (!observation.conditions.hunger_known || observation.conditions.hunger > 3.0)) continue;
        if (!std::isfinite(stack.hunger_restore) || !std::isfinite(stack.saturation_restore) ||
            stack.hunger_restore <= 0.0) continue;
        const double score = std::max(0.0, stack.hunger_restore) +
                             0.45 * std::max(0.0, stack.saturation_restore);
        if (score > best_score) {
            best = &stack;
            best_score = score;
        }
    }
    return best;
}

void set_skill_confidence(GameplayDecision& decision, const SkillExperienceBook* experience,
                          double baseline = 0.58) {
    const double efficacy = experience != nullptr ? experience->effectiveness(decision.skill) : 0.5;
    decision.confidence = clamp_value(baseline + (efficacy - 0.5) * 0.35, 0.05, 0.95);
}

bool valid_melee_reach(const GameplayCalibration& calibration) {
    return calibration.melee_reach.has_value() && std::isfinite(*calibration.melee_reach) &&
           *calibration.melee_reach >= 1.0 && *calibration.melee_reach <= 6.0;
}

bool safe_pearl_solution(const GameplayObservation& observation, ProjectileSolution* solution) {
    if (!observation.conditions.safe_pearl_landing_known || !has_item(observation, ItemKind::EnderPearl) ||
        !observation.calibration.pearl.calibrated || !finite_vec(observation.conditions.safe_pearl_landing)) {
        return false;
    }
    ProjectileRequest request;
    request.origin = observation.world.player.position;
    request.origin.y += 1.62;
    request.target_position = observation.conditions.safe_pearl_landing;
    request.latency_seconds = clamp_value(observation.latency_ms / 1000.0, 0.0, 0.75);
    const ProjectileSolution candidate = ProjectileSolver{}.solve(observation.calibration.pearl, request);
    if (!candidate.valid) return false;
    if (solution != nullptr) *solution = candidate;
    return true;
}

BrainAction movement_action(MovementStyle style) {
    switch (style) {
        case MovementStyle::Approach: return BrainAction::Approach;
        case MovementStyle::Retreat: return BrainAction::Retreat;
        case MovementStyle::Strafe:
        case MovementStyle::Circle: return BrainAction::Strafe;
        case MovementStyle::Recover: return BrainAction::Reposition;
        case MovementStyle::Stop: return BrainAction::Wait;
    }
    return BrainAction::Observe;
}

} // namespace

const std::vector<SkillDefinition>& gameplay_skill_catalog() {
    static const std::vector<SkillDefinition> catalog{
        {SkillId::ObserveAndWait, "observe_and_wait", SkillFamily::Perception, "Refresh state and wait when a safe action is not established.", false, true},
        {SkillId::ChooseMeleeStyle, "choose_melee_style", SkillFamily::Planning, "Select aggressive, defensive, strafing, chase or retreat combat from relative risk.", false, true},
        {SkillId::SwordStrafeCombo, "sword_strafe_combo", SkillFamily::Combat, "Aim at a tracked target while using measured lateral movement and attack timing.", true, true},
        {SkillId::AxePunish, "axe_punish", SkillFamily::Combat, "Use an axe only when this Bedrock server profile confirms an advantage over the sword; never assume Java shield-stun rules.", true, true},
        {SkillId::ShieldDefence, "shield_defence", SkillFamily::Combat, "Raise a shield when the threat and timing justify sacrificing an attack window.", true, true},
        {SkillId::BowLeadShot, "bow_lead_shot", SkillFamily::Ranged, "Solve a calibrated projectile trajectory against predicted target motion.", true, true},
        {SkillId::CrossbowShot, "crossbow_shot", SkillFamily::Ranged, "Aim and fire a loaded crossbow with a calibrated projectile profile.", true, true},
        {SkillId::MaceSmash, "mace_smash", SkillFamily::Combat, "Use a mace only when an observed smash window and safe landing context are available.", true, true},
        {SkillId::WindChargeKnockback, "wind_charge_knockback", SkillFamily::Combat, "Aim a calibrated wind charge at the target's feet/ground region for useful knockback.", true, true},
        {SkillId::WindChargeClutch, "wind_charge_clutch", SkillFamily::Survival, "Use a practised wind-charge recovery only when its movement profile is calibrated.", true, true},
        {SkillId::PearlEscape, "pearl_escape", SkillFamily::Survival, "Throw an ender pearl only toward a known-safe landing with a valid trajectory.", true, true},
        {SkillId::PearlChase, "pearl_chase", SkillFamily::Combat, "Use a pearl to close distance only when the chase landing is known to be safe.", true, true},
        {SkillId::WaterBucketClutch, "water_bucket_clutch", SkillFamily::Survival, "Place water at a validated clutch site when the dimension and server permit it.", true, true},
        {SkillId::BlockClutch, "block_clutch", SkillFamily::Survival, "Place a block at an observed valid support position during a fall.", true, true},
        {SkillId::EatFood, "eat_food", SkillFamily::Survival, "Choose available food from hunger need and defer eating during immediate danger.", false, true},
        {SkillId::GoldenAppleHeal, "golden_apple_heal", SkillFamily::Survival, "Consume a golden apple when its healing/effects justify the item cost and interruption risk.", false, true},
        {SkillId::DrinkHealingPotion, "drink_healing_potion", SkillFamily::Survival, "Drink a healing potion when the recovery window is safe enough.", false, true},
        {SkillId::SplashHealingPotion, "splash_healing_potion", SkillFamily::Survival, "Throw a splash healing potion only when its trajectory and self/target outcome are supported.", true, true},
        {SkillId::ExtinguishFire, "extinguish_fire", SkillFamily::Survival, "Choose water or milk based on dimension and known side effects.", false, true},
        {SkillId::EscapeWeb, "escape_web", SkillFamily::Movement, "Break or move out of cobweb using observed motion and the available tool.", false, true},
        {SkillId::SafeBridge, "safe_bridge", SkillFamily::Building, "Advance using validated support blocks and safe placement geometry.", true, true},
        {SkillId::SpeedBridge, "speed_bridge", SkillFamily::Building, "Use a calibrated fast-bridge timing profile with recovery conditions.", true, true},
        {SkillId::DiagonalBridge, "diagonal_bridge", SkillFamily::Building, "Build diagonally with a measured placement and movement sequence.", true, true},
        {SkillId::StaircaseBridge, "staircase_bridge", SkillFamily::Building, "Build a staircase while validating headroom and each support block.", true, true},
        {SkillId::MineObjective, "mine_objective", SkillFamily::Resource, "Mine a known objective block with an appropriate available tool.", false, true},
        {SkillId::ResourceGathering, "resource_gathering", SkillFamily::Resource, "Select mining/resource actions using the target and tool observations.", false, true},
        {SkillId::BedWarsRush, "bedwars_rush", SkillFamily::Planning, "Prioritize safe team/objective progress in a BedWars context.", true, true},
        {SkillId::BedWarsDefend, "bedwars_defend", SkillFamily::Planning, "Protect the bed/objective and manage incoming enemy pressure.", true, true},
        {SkillId::SMPTravel, "smp_travel", SkillFamily::Planning, "Navigate toward a known route target while managing hazards and supplies.", false, true},
        {SkillId::DodgeProjectile, "dodge_projectile", SkillFamily::Movement, "Move out of a predicted projectile path using observed motion.", true, true},
        {SkillId::SprintJump, "sprint_jump", SkillFamily::Movement, "Use sprint-jump movement only where collision, headroom and edge checks permit.", true, true},
        {SkillId::CircleStrafe, "circle_strafe", SkillFamily::Movement, "Maintain a measured circle around the current target while tracking aim.", true, true},
        {SkillId::RetreatAndReset, "retreat_and_reset", SkillFamily::Planning, "Create distance, heal or restore position rather than forcing a losing exchange.", false, true},
        {SkillId::SwitchToBestWeapon, "switch_to_best_weapon", SkillFamily::Combat, "Select the available weapon that best matches range and observed combat context.", false, true},
        {SkillId::EmergencyReposition, "emergency_reposition", SkillFamily::Movement, "Move to a validated safer position when continuing the current action is unsafe.", false, true}
    };
    return catalog;
}

void SkillExperienceBook::record(SkillId id, bool success, double reward) {
    const std::size_t index = skill_index(id);
    if (index >= records_.size() || !std::isfinite(reward)) return;
    SkillExperienceRecord& record = records_[index];
    if (record.attempts == std::numeric_limits<std::uint64_t>::max()) return;
    const double bounded_reward = clamp_value(reward, 0.0, 1.0);
    ++record.attempts;
    if (success && record.successes < std::numeric_limits<std::uint64_t>::max()) ++record.successes;
    if (record.attempts == 1U) record.mean_reward = bounded_reward;
    else {
        const double divisor = static_cast<double>(record.attempts);
        record.mean_reward += (bounded_reward - record.mean_reward) / divisor;
    }
}

SkillExperienceRecord SkillExperienceBook::get(SkillId id) const {
    const std::size_t index = skill_index(id);
    if (index >= records_.size()) return {};
    return records_[index];
}

double SkillExperienceBook::effectiveness(SkillId id) const {
    const SkillExperienceRecord record = get(id);
    const double attempts = static_cast<double>(record.attempts);
    const double posterior = (static_cast<double>(record.successes) + 1.0) / (attempts + 2.0);
    const double reward = clamp_value(record.mean_reward, 0.0, 1.0);
    // Beta-smoothed success and measured reward jointly inform later decisions.
    return clamp_value(0.65 * posterior + 0.35 * reward, 0.0, 1.0);
}

bool SkillExperienceBook::save_tsv(const std::string& path, std::string* error) const {
    auto set_error = [error](const std::string& message) { if (error != nullptr) *error = message; };
    if (path.empty()) {
        set_error("experience path is empty");
        return false;
    }
    const std::filesystem::path target(path);
    const std::filesystem::path temporary(path + ".tmp");
    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            set_error("cannot create experience directory: " + ec.message());
            return false;
        }
    }
    {
        std::ofstream stream(temporary, std::ios::out | std::ios::trunc);
        if (!stream) {
            set_error("cannot create experience temporary file");
            return false;
        }
        stream << "BEDROCKAI_SKILL_MEMORY_V1\n" << std::setprecision(17);
        for (std::size_t i = 0; i < records_.size(); ++i) {
            const SkillExperienceRecord& record = records_[i];
            stream << i << '\t' << record.attempts << '\t' << record.successes << '\t'
                   << record.mean_reward << '\n';
        }
        stream.flush();
        if (!stream) {
            set_error("failed writing experience data");
            stream.close();
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }
    std::filesystem::permissions(temporary,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, ec);
    if (ec) {
        set_error("cannot restrict experience-file permissions: " + ec.message());
        std::filesystem::remove(temporary, ec);
        return false;
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        set_error("cannot commit experience file without replacing the previous copy: " + ec.message());
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return false;
    }
    return true;
}

bool SkillExperienceBook::load_tsv(const std::string& path, std::string* error) {
    auto set_error = [error](const std::string& message) { if (error != nullptr) *error = message; };
    if (path.empty()) {
        set_error("experience path is empty");
        return false;
    }
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes > 1024U * 1024U) {
        set_error(ec ? "cannot stat experience file" : "experience file exceeds size limit");
        return false;
    }
    std::ifstream stream(path);
    if (!stream) {
        set_error("cannot open experience file");
        return false;
    }
    std::string header;
    if (!std::getline(stream, header) || header != "BEDROCKAI_SKILL_MEMORY_V1") {
        set_error("experience file header/version is invalid");
        return false;
    }
    std::array<SkillExperienceRecord, static_cast<std::size_t>(SkillId::Count)> parsed{};
    std::array<bool, static_cast<std::size_t>(SkillId::Count)> seen{};
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream row(line);
        std::size_t index = 0U;
        SkillExperienceRecord record{};
        if (!(row >> index >> record.attempts >> record.successes >> record.mean_reward)) {
            set_error("experience record is malformed");
            return false;
        }
        std::string extra;
        if (row >> extra || index >= parsed.size() || seen[index] ||
            record.successes > record.attempts || !std::isfinite(record.mean_reward) ||
            record.mean_reward < 0.0 || record.mean_reward > 1.0 || record.attempts > 1000000000000ULL) {
            set_error("experience record violates bounds or uniqueness");
            return false;
        }
        parsed[index] = record;
        seen[index] = true;
    }
    if (!stream.eof()) {
        set_error("error reading experience file");
        return false;
    }
    records_ = parsed;
    return true;
}

GameplayBrain::GameplayBrain(GameplayBrainConfig config) : config_(config) {}

GameplayDecision GameplayBrain::decide(const GameplayObservation& observation,
                                       const SkillExperienceBook* experience) const {
    GameplayDecision out;
    const WorldSnapshot& world = observation.world;
    if (!world.joined || !finite_vec(world.player.position) || !finite_vec(world.player.velocity) ||
        !std::isfinite(world.player.health) || world.player.health < 0.0 ||
        !std::isfinite(observation.latency_ms) || observation.latency_ms < 0.0 || observation.latency_ms > 10000.0) {
        out.action = BrainAction::Observe;
        out.skill = SkillId::ObserveAndWait;
        out.valid = false;
        out.reason = "world state is not joined, finite, or fresh enough for autonomous action";
        return out;
    }

    const double max_health = std::isfinite(observation.conditions.maximum_health) &&
                              observation.conditions.maximum_health > 0.0 &&
                              observation.conditions.maximum_health <= 2048.0
                                  ? observation.conditions.maximum_health : 20.0;
    const double health_fraction = clamp_value(world.player.health / max_health, 0.0, 1.0);
    const bool falling = world.player.airborne && world.player.velocity.y < -4.0;
    const bool void_emergency = observation.conditions.falling_toward_void ||
                                (observation.conditions.near_void_edge && falling);
    const int enemies = hostile_count(world);
    const auto target_id = select_target(observation);
    const EntityState* target = target_id.has_value() ? entity_by_id(world, *target_id) : nullptr;
    double target_distance = std::numeric_limits<double>::infinity();
    if (target != nullptr) target_distance = distance_3d(world.player.position, target->position);

    // 1. Falling into a known void takes precedence over all combat and inventory goals.
    if (void_emergency) {
        out.emergency = true;
        ProjectileSolution pearl_solution;
        struct RecoveryOption { double score; SkillId skill; BrainAction action; std::optional<int> slot; std::optional<ProjectileSolution> projectile; const char* reason; };
        std::vector<RecoveryOption> options;
        if (observation.conditions.wind_charge_recovery_calibrated && has_item(observation, ItemKind::WindCharge)) {
            const auto* item = find_item(observation, ItemKind::WindCharge);
            const double learned = experience != nullptr ? experience->effectiveness(SkillId::WindChargeClutch) : 0.5;
            options.push_back({0.86 + (learned - 0.5) * 0.25, SkillId::WindChargeClutch,
                               BrainAction::UseWindCharge, item != nullptr ? std::optional<int>(item->slot) : std::nullopt,
                               std::nullopt, "falling toward the void; a wind-charge recovery profile is calibrated"});
        }
        if (safe_pearl_solution(observation, &pearl_solution)) {
            const auto* item = find_item(observation, ItemKind::EnderPearl);
            const double learned = experience != nullptr ? experience->effectiveness(SkillId::PearlEscape) : 0.5;
            options.push_back({0.83 + (learned - 0.5) * 0.25, SkillId::PearlEscape,
                               BrainAction::ThrowPearl, item != nullptr ? std::optional<int>(item->slot) : std::nullopt,
                               pearl_solution, "falling toward the void; pearl destination and trajectory are validated"});
        }
        if (observation.conditions.water_clutch_site_known && observation.conditions.water_placement_allowed &&
            !observation.conditions.in_nether && has_item(observation, ItemKind::WaterBucket)) {
            const auto* item = find_item(observation, ItemKind::WaterBucket);
            const double learned = experience != nullptr ? experience->effectiveness(SkillId::WaterBucketClutch) : 0.5;
            options.push_back({0.80 + (learned - 0.5) * 0.25, SkillId::WaterBucketClutch,
                               BrainAction::WaterClutch, item != nullptr ? std::optional<int>(item->slot) : std::nullopt,
                               std::nullopt, "falling toward the void; water clutch site is known and water placement is allowed"});
        }
        if (observation.conditions.block_clutch_site_known && observation.conditions.block_placement_allowed &&
            has_item(observation, ItemKind::BuildingBlock)) {
            const auto* item = find_item(observation, ItemKind::BuildingBlock);
            const double learned = experience != nullptr ? experience->effectiveness(SkillId::BlockClutch) : 0.5;
            options.push_back({0.76 + (learned - 0.5) * 0.25, SkillId::BlockClutch,
                               BrainAction::BlockClutch, item != nullptr ? std::optional<int>(item->slot) : std::nullopt,
                               std::nullopt, "falling toward the void; a block-clutch placement site is validated"});
        }
        if (options.empty()) {
            const auto* dynamic_clutch = find_dynamic_item(observation, ItemCapability::ClutchRecovery);
            const ItemActivationMode clutch_activation = dynamic_clutch != nullptr
                ? preferred_action(*dynamic_clutch, {ItemActivationMode::Throw, ItemActivationMode::Place, ItemActivationMode::Use})
                : ItemActivationMode::Unknown;
            if (dynamic_clutch != nullptr && clutch_activation != ItemActivationMode::Unknown) {
                out.action = BrainAction::UseAdaptiveItem;
                out.skill = SkillId::EmergencyReposition;
                out.item_slot = dynamic_clutch->slot;
                out.item_identifier = item_identity(*dynamic_clutch);
                out.item_activation = clutch_activation;
                out.valid = true;
                out.emergency = true;
                out.reason = "void emergency: a server-verified custom recovery item is available; its registered activation mode will be used";
                set_skill_confidence(out, experience, 0.55 + 0.2 * dynamic_clutch->profile_confidence);
                return out;
            }
            out.action = BrainAction::NoSafeRecoveryAction;
            out.skill = SkillId::EmergencyReposition;
            out.valid = true;
            out.reason = "void emergency detected, but no available recovery has both required item and validated conditions";
            set_skill_confidence(out, experience, 0.4);
            return out;
        }
        const auto best = std::max_element(options.begin(), options.end(),
            [](const RecoveryOption& a, const RecoveryOption& b) { return a.score < b.score; });
        out.action = best->action;
        out.skill = best->skill;
        out.item_slot = best->slot;
        out.projectile = best->projectile;
        out.reason = best->reason;
        out.valid = true;
        set_skill_confidence(out, experience, 0.7);
        return out;
    }

    // 2. Recover from fire/web before choosing a voluntary combat plan.
    if (observation.conditions.burning) {
        if (!observation.conditions.in_nether && has_item(observation, ItemKind::WaterBucket)) {
            const auto* item = find_item(observation, ItemKind::WaterBucket);
            out.action = BrainAction::ExtinguishFire;
            out.skill = SkillId::ExtinguishFire;
            out.item_slot = item != nullptr ? std::optional<int>(item->slot) : std::nullopt;
            out.reason = "burning; water bucket available outside the Nether";
        } else if (const auto* custom_extinguisher = find_dynamic_item(observation, ItemCapability::FireControl);
                   custom_extinguisher != nullptr &&
                   preferred_action(*custom_extinguisher, {ItemActivationMode::Use, ItemActivationMode::Place, ItemActivationMode::Consume}) != ItemActivationMode::Unknown) {
            out.action = BrainAction::UseAdaptiveItem;
            out.skill = SkillId::ExtinguishFire;
            out.item_slot = custom_extinguisher->slot;
            out.item_identifier = item_identity(*custom_extinguisher);
            out.item_activation = preferred_action(*custom_extinguisher, {ItemActivationMode::Use, ItemActivationMode::Place, ItemActivationMode::Consume});
            out.emergency = true;
            out.valid = true;
            out.reason = "burning; a verified server-specific fire-control item is available";
            set_skill_confidence(out, experience, 0.55 + 0.2 * custom_extinguisher->profile_confidence);
            return out;
        } else {
            out.action = BrainAction::Retreat;
            out.skill = SkillId::RetreatAndReset;
            out.emergency = true;
            out.reason = "burning; no safe extinguishing item is available, so create distance";
            MovementTarget retreat{world.player.position, MovementStyle::Retreat, false, true, false, 2.0, 1.0};
            out.movement = MovementController{}.compute(world, retreat);
        }
        out.valid = true;
        set_skill_confidence(out, experience, 0.62);
        return out;
    }
    if (observation.conditions.in_web) {
        const auto* custom_web_tool = find_dynamic_item(observation, ItemCapability::WebEscape);
        if (custom_web_tool != nullptr &&
            preferred_action(*custom_web_tool, {ItemActivationMode::Attack, ItemActivationMode::Mine, ItemActivationMode::Use}) != ItemActivationMode::Unknown) {
            out.action = BrainAction::UseAdaptiveItem;
            out.item_slot = custom_web_tool->slot;
            out.item_identifier = item_identity(*custom_web_tool);
            out.item_activation = preferred_action(*custom_web_tool, {ItemActivationMode::Attack, ItemActivationMode::Mine, ItemActivationMode::Use});
            out.skill = SkillId::EscapeWeb;
            out.valid = true;
            out.reason = "inside a web; a verified server-specific escape tool is selected and movement continues";
        } else {
            out.action = BrainAction::EscapeWeb;
            out.skill = SkillId::EscapeWeb;
            out.valid = true;
        }
        out.movement.forward = 1.0;
        out.movement.strafe = world.player.sprinting ? -0.5 : 0.5;
        out.movement.jump = world.player.on_ground;
        out.movement.sprint = world.player.on_ground;
        out.movement.valid = true;
        if (out.reason.empty()) out.reason = "inside a cobweb; jump and vary the movement vector while leaving the web";
        set_skill_confidence(out, experience, 0.55);
        return out;
    }

    // 3. Keep the character fed, but never begin a normal eating action in immediate danger.
    if (observation.conditions.hunger_known && std::isfinite(observation.conditions.hunger) &&
        observation.conditions.hunger <= config_.eat_hunger_threshold && !observation.conditions.immediate_threat &&
        enemies == 0) {
        const InventoryStackObservation* food = best_food(observation);
        if (food != nullptr) {
            out.action = BrainAction::EatFood;
            out.skill = SkillId::EatFood;
            out.item_slot = food->slot;
            if (food->kind == ItemKind::Unknown && !item_identity(*food).empty()) out.item_identifier = item_identity(*food);
            out.reason = "hunger is below the configured threshold and no hostile threat is currently observed";
            out.valid = true;
            set_skill_confidence(out, experience, 0.62);
            return out;
        }
    }

    const auto* golden_apple = find_item(observation, ItemKind::GoldenApple);
    const auto* enchanted_apple = find_item(observation, ItemKind::EnchantedGoldenApple);
    const auto* healing_potion = find_item(observation, ItemKind::HealingPotion);
    if (health_fraction <= config_.critical_health_fraction && enemies > 0) {
        // Do not eat or drink through imminent damage unless the host has explicitly
        // observed a safe use window. First prefer a verified safe escape if one exists.
        ProjectileSolution pearl_solution;
        if (safe_pearl_solution(observation, &pearl_solution) && observation.conditions.immediate_threat) {
            const auto* item = find_item(observation, ItemKind::EnderPearl);
            out.action = BrainAction::ThrowPearl;
            out.skill = SkillId::PearlEscape;
            out.item_slot = item != nullptr ? std::optional<int>(item->slot) : std::nullopt;
            out.projectile = pearl_solution;
            out.emergency = true;
            out.valid = true;
            out.reason = "critical health with immediate threat; safe pearl escape is validated";
            set_skill_confidence(out, experience, 0.68);
            return out;
        }
        if (observation.conditions.safe_heal_window) {
            if (enchanted_apple != nullptr || golden_apple != nullptr) {
                const auto* item = enchanted_apple != nullptr ? enchanted_apple : golden_apple;
                out.action = BrainAction::EatGoldenApple;
                out.skill = SkillId::GoldenAppleHeal;
                out.item_slot = item->slot;
                out.valid = true;
                out.reason = "critical health and a safe healing window is explicitly observed";
                set_skill_confidence(out, experience, 0.72);
                return out;
            }
            if (healing_potion != nullptr) {
                out.action = BrainAction::DrinkHealingPotion;
                out.skill = SkillId::DrinkHealingPotion;
                out.item_slot = healing_potion->slot;
                out.valid = true;
                out.reason = "critical health; healing-potion use window is explicitly observed";
                set_skill_confidence(out, experience, 0.68);
                return out;
            }
        }
        out.action = BrainAction::Retreat;
        out.skill = SkillId::RetreatAndReset;
        out.combat_style = CombatStyle::Retreat;
        out.emergency = true;
        out.valid = true;
        out.reason = "critical health under hostile pressure; retreat instead of assuming eating or drinking is safe";
        if (target != nullptr) {
            MovementTarget retreat{target->position, MovementStyle::Retreat, true, true, false, 4.0, 1.0};
            out.movement = MovementController{}.compute(world, retreat);
        }
        set_skill_confidence(out, experience, 0.66);
        return out;
    }

    // Moderate-health healing uses explicit safe-window evidence, not a fixed unconditional rule.
    if (health_fraction <= config_.heal_health_fraction && observation.conditions.safe_heal_window) {
        if (enchanted_apple != nullptr || golden_apple != nullptr) {
            const auto* item = enchanted_apple != nullptr ? enchanted_apple : golden_apple;
            out.action = BrainAction::EatGoldenApple;
            out.skill = SkillId::GoldenAppleHeal;
            out.item_slot = item->slot;
            out.valid = true;
            out.reason = "health is below the healing threshold and the use window is explicitly safe";
            set_skill_confidence(out, experience, 0.64);
            return out;
        }
        if (healing_potion != nullptr) {
            out.action = BrainAction::DrinkHealingPotion;
            out.skill = SkillId::DrinkHealingPotion;
            out.item_slot = healing_potion->slot;
            out.valid = true;
            out.reason = "health is below the healing threshold and a healing potion is available";
            set_skill_confidence(out, experience, 0.62);
            return out;
        }
    }

    // 4. Mace and wind-charge actions are gated by explicit opportunity signals.
    if (target != nullptr && observation.game_mode == GameMode::MacePvP &&
        observation.conditions.mace_smash_window_known && observation.conditions.mace_smash_window_open &&
        has_item(observation, ItemKind::Mace)) {
        const auto* item = find_item(observation, ItemKind::Mace);
        out.action = BrainAction::UseMace;
        out.skill = SkillId::MaceSmash;
        out.item_slot = item != nullptr ? std::optional<int>(item->slot) : std::nullopt;
        out.target_runtime_id = target->runtime_id;
        out.target_position = target->position;
        out.valid = true;
        out.reason = "Mace PvP mode and an explicitly observed mace-smash window are both present";
        set_skill_confidence(out, experience, 0.66);
        return out;
    }
    if (target != nullptr && observation.conditions.wind_charge_knockback_window_known &&
        observation.conditions.wind_charge_knockback_window_open && has_item(observation, ItemKind::WindCharge) &&
        observation.calibration.wind_charge.calibrated) {
        const auto* item = find_item(observation, ItemKind::WindCharge);
        ProjectileRequest request;
        request.origin = world.player.position;
        request.origin.y += 1.62;
        request.target_position = target->position;
        request.target_position.y += 0.15; // target's foot/ground region for knockback setup
        request.target_velocity = target->velocity;
        request.latency_seconds = clamp_value(observation.latency_ms / 1000.0, 0.0, 0.75);
        const ProjectileSolution solution = ProjectileSolver{}.solve(observation.calibration.wind_charge, request);
        if (solution.valid) {
            out.action = BrainAction::UseWindCharge;
            out.skill = SkillId::WindChargeKnockback;
            out.item_slot = item != nullptr ? std::optional<int>(item->slot) : std::nullopt;
            out.projectile = solution;
            out.target_runtime_id = target->runtime_id;
            out.target_position = target->position;
            out.valid = true;
            out.reason = "wind-charge knockback window is observed and the calibrated trajectory meets tolerance";
            set_skill_confidence(out, experience, 0.62);
            return out;
        }
    }

    // 5. Defensive tool/weapon choices are based on observed opponent state.
    if (target != nullptr && observation.conditions.enemy_attacking && has_item(observation, ItemKind::Shield)) {
        const auto* shield = find_item(observation, ItemKind::Shield);
        out.action = BrainAction::RaiseShield;
        out.skill = SkillId::ShieldDefence;
        out.item_slot = shield != nullptr ? std::optional<int>(shield->slot) : std::nullopt;
        out.target_runtime_id = target->runtime_id;
        out.target_position = target->position;
        out.valid = true;
        out.reason = "an enemy attack is observed and a shield is available; defend before selecting the next action";
        set_skill_confidence(out, experience, 0.6);
        return out;
    }
    const bool confirmed_axe_counter = target != nullptr && observation.conditions.target_using_shield_known &&
        observation.conditions.target_using_shield && observation.conditions.server_axe_shield_punish_supported &&
        has_item(observation, ItemKind::Axe);
    if (confirmed_axe_counter) {
        const auto* axe = find_item(observation, ItemKind::Axe);
        if (axe != nullptr && world.player.selected_slot >= 0 && axe->slot != world.player.selected_slot) {
            out.action = BrainAction::SwitchWeapon;
            out.skill = SkillId::AxePunish;
            out.item_slot = axe->slot;
            out.target_runtime_id = target->runtime_id;
            out.target_position = target->position;
            out.valid = true;
            out.reason = "the target shield and server-specific axe advantage are confirmed; switch to the axe";
            set_skill_confidence(out, experience, 0.58);
            return out;
        }
    } else if (target != nullptr && target_distance <= 8.0 &&
               find_custom_melee_weapon(observation) != nullptr &&
               (!has_item(observation, ItemKind::Sword) ||
                find_custom_melee_weapon(observation)->profile_confidence > 0.72)) {
        const auto* custom_weapon = find_custom_melee_weapon(observation);
        if (custom_weapon != nullptr && world.player.selected_slot >= 0 && custom_weapon->slot != world.player.selected_slot) {
            out.action = BrainAction::SwitchWeapon;
            out.skill = SkillId::SwitchToBestWeapon;
            out.item_slot = custom_weapon->slot;
            out.item_identifier = item_identity(*custom_weapon);
            out.target_runtime_id = target->runtime_id;
            out.target_position = target->position;
            out.valid = true;
            out.reason = "a server-verified custom melee item is preferred by its learned capability profile over the ordinary sword fallback";
            set_skill_confidence(out, experience, 0.55 + 0.25 * custom_weapon->profile_confidence);
            return out;
        }
    } else if (target != nullptr && has_item(observation, ItemKind::Sword)) {
        const auto* sword = find_item(observation, ItemKind::Sword);
        if (sword != nullptr && world.player.selected_slot >= 0 && sword->slot != world.player.selected_slot &&
            target_distance <= 8.0) {
            out.action = BrainAction::SwitchWeapon;
            out.skill = SkillId::SwitchToBestWeapon;
            out.item_slot = sword->slot;
            out.target_runtime_id = target->runtime_id;
            out.target_position = target->position;
            out.valid = true;
            out.reason = "sword is used only as a fallback when no better verified context-specific melee profile is selected";
            set_skill_confidence(out, experience, 0.56);
            return out;
        }
    }

    // Fireballs are a server/mode-specific skill; never infer support from the item alone.
    if (target != nullptr && observation.game_mode == GameMode::BedWars &&
        observation.conditions.fireball_use_supported && has_item(observation, ItemKind::Fireball) &&
        observation.conditions.line_of_sight_known && observation.conditions.line_of_sight &&
        observation.calibration.fireball.calibrated) {
        const auto* fireball = find_item(observation, ItemKind::Fireball);
        ProjectileRequest request;
        request.origin = world.player.position;
        request.origin.y += 1.62;
        request.target_position = target->position;
        request.target_position.y += 0.5;
        request.target_velocity = target->velocity;
        request.latency_seconds = clamp_value(observation.latency_ms / 1000.0, 0.0, 0.75);
        const ProjectileSolution solution = ProjectileSolver{}.solve(observation.calibration.fireball, request);
        if (solution.valid) {
            out.action = BrainAction::UseFireball;
            out.skill = SkillId::BedWarsRush;
            out.item_slot = fireball != nullptr ? std::optional<int>(fireball->slot) : std::nullopt;
            out.projectile = solution;
            out.target_runtime_id = target->runtime_id;
            out.target_position = target->position;
            out.aim.valid = true;
            out.aim.desired_yaw = solution.yaw_degrees;
            out.aim.desired_pitch = solution.pitch_degrees;
            out.aim.yaw_delta = solution.yaw_degrees - world.player.rotation.yaw;
            out.aim.pitch_delta = solution.pitch_degrees - world.player.rotation.pitch;
            out.valid = true;
            out.reason = "BedWars fireball support, target visibility, item availability and calibrated trajectory are confirmed";
            set_skill_confidence(out, experience, 0.59);
            return out;
        }
    }

    // Splash-healing is only used when an explicit safe-use signal and calibrated throw exist.
    if (target != nullptr && observation.conditions.splash_potion_use_safe &&
        health_fraction <= config_.heal_health_fraction && has_item(observation, ItemKind::SplashHealingPotion) &&
        observation.calibration.potion.calibrated && !observation.conditions.immediate_threat) {
        const auto* potion = find_item(observation, ItemKind::SplashHealingPotion);
        ProjectileRequest request;
        request.origin = world.player.position;
        request.origin.y += 1.45;
        request.target_position = world.player.position;
        request.target_position.y += 0.2;
        request.latency_seconds = clamp_value(observation.latency_ms / 1000.0, 0.0, 0.5);
        const ProjectileSolution solution = ProjectileSolver{}.solve(observation.calibration.potion, request);
        if (solution.valid) {
            out.action = BrainAction::ThrowHealingPotion;
            out.skill = SkillId::SplashHealingPotion;
            out.item_slot = potion != nullptr ? std::optional<int>(potion->slot) : std::nullopt;
            out.projectile = solution;
            out.valid = true;
            out.reason = "a safe splash-potion use window and calibrated self-heal trajectory are explicitly observed";
            set_skill_confidence(out, experience, 0.57);
            return out;
        }
    }

    // 6. Execute a ranged skill only when target visibility, ammunition and trajectory are known.
    if (target != nullptr && observation.conditions.line_of_sight_known && observation.conditions.line_of_sight &&
        target_distance >= config_.projectile_min_distance) {
        const bool has_arrow = has_ammunition(observation, ItemKind::Arrow);
        const InventoryStackObservation* bow = find_item(observation, ItemKind::Bow);
        const InventoryStackObservation* crossbow = find_item(observation, ItemKind::Crossbow);
        const ProjectilePhysics* physics = &observation.calibration.arrow;
        SkillId ranged_skill = SkillId::BowLeadShot;
        BrainAction ranged_action = BrainAction::AimAndShootBow;
        const InventoryStackObservation* weapon = bow;
        if (weapon == nullptr && crossbow != nullptr) {
            weapon = crossbow;
            ranged_skill = SkillId::CrossbowShot;
            ranged_action = BrainAction::AimAndShootCrossbow;
        }
        if (weapon != nullptr && has_arrow && physics->calibrated) {
            ProjectileRequest request;
            request.origin = world.player.position;
            request.origin.y += 1.62;
            request.target_position = target->position;
            request.target_position.y += config_.target_eye_height;
            request.target_velocity = target->velocity;
            request.latency_seconds = clamp_value(observation.latency_ms / 1000.0, 0.0, 0.75);
            const ProjectileSolution solution = ProjectileSolver{}.solve(*physics, request);
            if (solution.valid) {
                out.action = ranged_action;
                out.skill = ranged_skill;
                out.item_slot = weapon->slot;
                out.target_runtime_id = target->runtime_id;
                out.target_position = target->position;
                out.projectile = solution;
                out.aim.valid = true;
                out.aim.desired_yaw = solution.yaw_degrees;
                out.aim.desired_pitch = solution.pitch_degrees;
                out.aim.yaw_delta = solution.yaw_degrees - world.player.rotation.yaw;
                out.aim.pitch_delta = solution.pitch_degrees - world.player.rotation.pitch;
                out.valid = true;
                out.reason = "target is visible; ammunition and calibrated trajectory are available";
                set_skill_confidence(out, experience, 0.64);
                return out;
            }
        }
        const auto* custom_ranged = find_dynamic_item(observation, ItemCapability::RangedWeapon);
        if (custom_ranged != nullptr && custom_ranged->custom_projectile_physics.has_value() &&
            custom_ranged->custom_projectile_physics->calibrated &&
            preferred_action(*custom_ranged, {ItemActivationMode::Use, ItemActivationMode::Throw, ItemActivationMode::Attack}) != ItemActivationMode::Unknown) {
            ProjectileRequest request;
            request.origin = world.player.position;
            request.origin.y += 1.62;
            request.target_position = target->position;
            request.target_position.y += config_.target_eye_height;
            request.target_velocity = target->velocity;
            request.latency_seconds = clamp_value(observation.latency_ms / 1000.0, 0.0, 0.75);
            const ProjectileSolution solution = ProjectileSolver{}.solve(*custom_ranged->custom_projectile_physics, request);
            if (solution.valid) {
                out.action = BrainAction::UseAdaptiveItem;
                out.skill = SkillId::BowLeadShot;
                out.item_slot = custom_ranged->slot;
                out.item_identifier = item_identity(*custom_ranged);
                out.item_activation = preferred_action(*custom_ranged, {ItemActivationMode::Use, ItemActivationMode::Throw, ItemActivationMode::Attack});
                out.target_runtime_id = target->runtime_id;
                out.target_position = target->position;
                out.projectile = solution;
                out.aim.valid = true;
                out.aim.desired_yaw = solution.yaw_degrees;
                out.aim.desired_pitch = solution.pitch_degrees;
                out.aim.yaw_delta = solution.yaw_degrees - world.player.rotation.yaw;
                out.aim.pitch_delta = solution.pitch_degrees - world.player.rotation.pitch;
                out.valid = true;
                out.reason = "custom server-specific ranged item selected from its verified capability profile and calibrated projectile physics";
                set_skill_confidence(out, experience, 0.55 + 0.25 * custom_ranged->profile_confidence);
                return out;
            }
        }
    }

    // 6. Objective skills share the same observations and inventory model as combat.
    const auto* custom_mining_tool = find_dynamic_item(observation, ItemCapability::MiningTool);
    const bool custom_mining_ready = custom_mining_tool != nullptr && supports_action(*custom_mining_tool, ItemActivationMode::Mine);
    if ((observation.goal == GameplayGoal::Mine || observation.goal == GameplayGoal::Survive) &&
        observation.conditions.objective_block_known && (has_item(observation, ItemKind::Pickaxe) || custom_mining_ready)) {
        out.action = BrainAction::MineBlock;
        out.skill = observation.goal == GameplayGoal::Mine ? SkillId::MineObjective : SkillId::ResourceGathering;
        out.target_position = Vec3{static_cast<double>(observation.conditions.objective_block_x) + 0.5,
                                    static_cast<double>(observation.conditions.objective_block_y) + 0.5,
                                    static_cast<double>(observation.conditions.objective_block_z) + 0.5};
        const auto* pickaxe = find_item(observation, ItemKind::Pickaxe);
        out.item_slot = pickaxe != nullptr ? std::optional<int>(pickaxe->slot) : std::optional<int>(custom_mining_tool->slot);
        if (pickaxe == nullptr && custom_mining_tool != nullptr) {
            out.item_identifier = item_identity(*custom_mining_tool);
            out.item_activation = ItemActivationMode::Mine;
        }
        out.valid = true;
        out.reason = pickaxe != nullptr ? "a mineable objective block and pickaxe are explicitly observed"
                                        : "a verified server-specific mining tool and target block are observed";
        set_skill_confidence(out, experience, 0.58);
        return out;
    }
    const auto* custom_bridge_item = find_dynamic_item(observation, ItemCapability::BlockPlace);
    const bool custom_bridge_ready = custom_bridge_item != nullptr && supports_action(*custom_bridge_item, ItemActivationMode::Place);
    if ((observation.goal == GameplayGoal::Bridge || observation.goal == GameplayGoal::BedWarsRush) &&
        observation.conditions.objective_target_known && (has_item(observation, ItemKind::BuildingBlock) || custom_bridge_ready)) {
        out.action = BrainAction::Bridge;
        out.skill = observation.goal == GameplayGoal::BedWarsRush ? SkillId::BedWarsRush : SkillId::SafeBridge;
        out.target_position = observation.conditions.objective_target;
        const auto* building_blocks = find_item(observation, ItemKind::BuildingBlock);
        out.item_slot = building_blocks != nullptr ? std::optional<int>(building_blocks->slot) : std::optional<int>(custom_bridge_item->slot);
        if (building_blocks == nullptr && custom_bridge_item != nullptr) {
            out.item_identifier = item_identity(*custom_bridge_item);
            out.item_activation = ItemActivationMode::Place;
        }
        out.movement = MovementController{}.compute(world,
            MovementTarget{observation.conditions.objective_target, MovementStyle::Approach, true, true, false, 2.0, 1.0});
        out.valid = out.movement.valid;
        out.reason = building_blocks != nullptr
            ? "bridge objective and building blocks are observed; placement remains subject to block-face validation"
            : "bridge objective and a verified custom placeable item are observed; placement remains subject to block-face validation";
        set_skill_confidence(out, experience, 0.52);
        return out;
    }
    if (observation.goal == GameplayGoal::BedWarsDefend) {
        out.action = BrainAction::Reposition;
        out.skill = SkillId::BedWarsDefend;
        out.valid = true;
        out.reason = "BedWars defence goal selected; move to the host-supplied defended position before engaging";
        if (observation.conditions.objective_target_known) {
            out.target_position = observation.conditions.objective_target;
            out.movement = MovementController{}.compute(world,
                MovementTarget{observation.conditions.objective_target, MovementStyle::Approach, true, true, false, 2.0, 1.0});
        }
        set_skill_confidence(out, experience, 0.5);
        return out;
    }
    if ((observation.goal == GameplayGoal::Explore || observation.goal == GameplayGoal::SMPTravel) &&
        observation.conditions.objective_target_known) {
        out.action = BrainAction::Explore;
        out.skill = SkillId::SMPTravel;
        out.target_position = observation.conditions.objective_target;
        out.movement = MovementController{}.compute(world,
            MovementTarget{observation.conditions.objective_target, MovementStyle::Approach, true, true, false, 2.0, 1.0});
        out.valid = out.movement.valid;
        out.reason = "a travel/exploration waypoint is known; use normal movement and re-evaluate on each observation";
        set_skill_confidence(out, experience, 0.54);
        return out;
    }

    // 7. General combat policy. The existing low-level controller supplies aim and movement,
    // but it may attack only when a valid server-specific melee reach has been calibrated.
    if (target != nullptr) {
        const auto* verified_custom_weapon = find_custom_melee_weapon(observation);
        const double my_gear = observation.conditions.armour_points +
                               (has_item(observation, ItemKind::Sword) ? 3.0 : 0.0) +
                               (has_item(observation, ItemKind::Axe) ? 2.0 : 0.0) +
                               (has_item(observation, ItemKind::Mace) ? 2.0 : 0.0) +
                               (verified_custom_weapon != nullptr ? 2.0 * verified_custom_weapon->profile_confidence : 0.0);
        const CombatStyle style = adaptive_combat_style(observation, experience, health_fraction,
                                                        target_distance, enemies,
                                                        target->health > 0.0 && target->health <= config_.finish_target_health);

        CombatConfig combat_config;
        combat_config.attack_distance = observation.calibration.melee_reach;
        combat_config.attack_interval_seconds = observation.calibration.melee_interval_seconds;
        CombatCommand combat = CombatController(combat_config).decide(world, style);
        out.combat = combat;
        out.combat_style = style;
        out.target_runtime_id = target->runtime_id;
        out.target_position = target->position;
        out.movement = combat.movement;
        out.aim = combat.aim;
        if (combat.propose_attack && observation.server_mechanics.attack_model == AttackModel::CooldownGated) {
            bool cooldown_ready = observation.server_mechanics.full_damage_interval_ticks.has_value() &&
                                  world.player.last_attack_tick == 0U;
            if (!cooldown_ready && observation.server_mechanics.full_damage_interval_ticks.has_value() &&
                world.server_tick_known && world.server_tick >= world.player.last_attack_tick) {
                cooldown_ready = world.server_tick - world.player.last_attack_tick >=
                    static_cast<std::uint64_t>(*observation.server_mechanics.full_damage_interval_ticks);
            }
            if (!cooldown_ready) {
                combat.propose_attack = false;
                out.combat.propose_attack = false;
                out.action = combat.movement.valid ? BrainAction::Strafe : BrainAction::Wait;
                out.skill = SkillId::ChooseMeleeStyle;
                out.reason = !observation.server_mechanics.full_damage_interval_ticks.has_value()
                    ? "server evidence indicates cooldown-gated combat, but the weapon's full-damage interval is not calibrated; withholding a spam attack"
                    : "server evidence indicates cooldown-gated combat; waiting for a fresh protocol input tick to satisfy the measured interval";
                out.valid = combat.valid || out.movement.valid;
                set_skill_confidence(out, experience, 0.55);
                return out;
            }
        }
        if (combat.propose_attack) {
            out.action = BrainAction::Attack;
            out.skill = (style == CombatStyle::Strafing) ? SkillId::SwordStrafeCombo :
                        (style == CombatStyle::Defensive ? SkillId::RetreatAndReset : SkillId::ChooseMeleeStyle);
            out.reason = "target selected from live entity state; attack is proposed only inside calibrated melee reach";
        } else if (style == CombatStyle::Defensive || style == CombatStyle::EdgeVoid || health_fraction <= config_.retreat_health_fraction) {
            out.action = BrainAction::Retreat;
            out.skill = style == CombatStyle::EdgeVoid ? SkillId::EmergencyReposition : SkillId::RetreatAndReset;
            out.reason = "health, numerical disadvantage, or edge risk favours creating distance";
        } else {
            out.action = movement_action(combat.movement.valid && target_distance > 2.5 ? MovementStyle::Approach : MovementStyle::Circle);
            out.skill = out.action == BrainAction::Strafe ? SkillId::CircleStrafe : SkillId::ChooseMeleeStyle;
            out.reason = valid_melee_reach(observation.calibration)
                ? "move into calibrated reach while preserving target tracking"
                : "melee reach is not calibrated; approach but do not propose an attack";
        }
        out.valid = combat.valid || out.movement.valid;
        set_skill_confidence(out, experience, 0.56 + (my_gear > 0.0 ? 0.02 : 0.0));
        return out;
    }

    out.action = BrainAction::Observe;
    out.skill = SkillId::ObserveAndWait;
    out.valid = true;
    out.reason = "no target or concrete survival/objective action is established; refresh observations before acting";
    set_skill_confidence(out, experience, 0.52);
    return out;
}

} // namespace bedrockai
