#include "core/item_knowledge.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace bedrockai {
namespace {
std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string profile_key(const std::string& identifier, const std::string& variant_key) {
    return identifier + "\x1f" + variant_key;
}

bool safe_field(const std::string& value) {
    return value.find('\t') == std::string::npos && value.find('\n') == std::string::npos &&
           value.find('\r') == std::string::npos;
}

void add(ItemKnowledge& out, ItemCapability capability) {
    out.capabilities |= capability_mask(capability);
}

std::optional<ItemActivationMode> parse_activation(unsigned int value) {
    if (value > static_cast<unsigned int>(ItemActivationMode::Interact)) return std::nullopt;
    return static_cast<ItemActivationMode>(value);
}

bool valid_projectile(const ProjectilePhysics& p, bool require_calibrated) {
    if (!std::isfinite(p.launch_speed) || !std::isfinite(p.gravity) || !std::isfinite(p.drag_per_tick) ||
        !std::isfinite(p.tick_rate) || !std::isfinite(p.max_flight_seconds) || !std::isfinite(p.hit_tolerance)) return false;
    if (!p.calibrated) return !require_calibrated;
    return p.launch_speed > 0.0 && p.gravity >= 0.0 && p.drag_per_tick > 0.0 && p.drag_per_tick <= 1.0 &&
           p.tick_rate >= 5.0 && p.tick_rate <= 120.0 && p.max_flight_seconds > 0.0 && p.max_flight_seconds <= 10.0 &&
           p.hit_tolerance > 0.0 && p.hit_tolerance <= 4.0;
}
}

ItemKnowledge infer_item_knowledge(const ItemDescriptor& descriptor) {
    ItemKnowledge out;
    out.identifier = descriptor.identifier;
    out.display_name = descriptor.display_name;
    out.variant_key = descriptor.variant_key;
    const std::string id = lower_ascii(descriptor.identifier);
    const std::string name = lower_ascii(descriptor.display_name);
    const bool known_vanilla = id.rfind("minecraft:", 0U) == 0U;
    auto id_has = [&id](const std::string& token) { return id.find(token) != std::string::npos; };
    auto label_has = [&name](const std::string& token) { return name.find(token) != std::string::npos; };

    // Known vanilla identifiers get executable conventional activation semantics.
    if (known_vanilla) {
        if (id_has("_sword") || id == "minecraft:trident") {
            add(out, ItemCapability::MeleeWeapon);
            out.activation = ItemActivationMode::Attack;
            out.verified = true;
            out.confidence = 0.99;
            out.evidence = "known vanilla weapon identifier";
            if (id == "minecraft:trident") {
                add(out, ItemCapability::RangedWeapon); add(out, ItemCapability::Projectile); add(out, ItemCapability::Throwable);
                out.activations |= activation_mask(ItemActivationMode::Throw);
                out.evidence = "known vanilla trident supports melee and thrown modes; projectile physics still requires calibration";
            }
        }
        if (id_has("_axe")) {
            add(out, ItemCapability::MeleeWeapon);
            add(out, ItemCapability::MiningTool);
            out.activation = ItemActivationMode::Attack;
            out.verified = true;
            out.confidence = 0.98;
            out.evidence = "known vanilla axe identifier; server-specific combat advantages remain unassumed";
        }
        if (id_has("pickaxe") || id_has("shovel") || id_has("_hoe")) {
            add(out, ItemCapability::MiningTool);
            out.activation = ItemActivationMode::Mine;
            out.verified = true;
            out.confidence = 0.98;
            out.evidence = "known vanilla tool identifier";
        }
        if (id == "minecraft:bow" || id == "minecraft:crossbow") {
            add(out, ItemCapability::RangedWeapon);
            add(out, ItemCapability::Projectile);
            out.activation = ItemActivationMode::Attack;
            out.verified = true;
            out.confidence = 0.99;
            out.evidence = "known vanilla ranged weapon identifier";
        }
        if (id == "minecraft:shield") {
            add(out, ItemCapability::Defence);
            out.activation = ItemActivationMode::Defend;
            out.verified = true;
            out.confidence = 0.99;
            out.evidence = "known vanilla shield identifier";
        }
        if (id == "minecraft:snowball" || id == "minecraft:egg") {
            add(out, ItemCapability::Projectile); add(out, ItemCapability::Throwable);
            out.activation = ItemActivationMode::Throw; out.verified = true; out.confidence = 0.98;
            out.evidence = "known vanilla throwable; server-specific knockback/effect remains separate";
        }
        if (id == "minecraft:totem_of_undying") {
            add(out, ItemCapability::Defence); add(out, ItemCapability::Healing);
            out.activation = ItemActivationMode::Interact; out.verified = true; out.confidence = 0.98;
            out.evidence = "known vanilla offhand-triggered survival item; equipping it is distinct from using it";
        }
        if (id == "minecraft:elytra") {
            add(out, ItemCapability::Mobility);
            out.activation = ItemActivationMode::Interact; out.verified = true; out.confidence = 0.98;
            out.evidence = "known vanilla wearable mobility item";
        }
        if (id == "minecraft:firework_rocket") {
            add(out, ItemCapability::Mobility); add(out, ItemCapability::Projectile); add(out, ItemCapability::Throwable);
            out.activation = ItemActivationMode::Use; out.activations |= activation_mask(ItemActivationMode::Throw);
            out.verified = true; out.confidence = 0.96;
            out.evidence = "firework rocket; Elytra boost versus projectile use depends on context/server";
        }
        if (id == "minecraft:milk_bucket") {
            add(out, ItemCapability::Drinkable); add(out, ItemCapability::ResourceUtility);
            out.activation = ItemActivationMode::Consume; out.verified = true; out.confidence = 0.98;
            out.evidence = "milk removes status effects; it does not extinguish burning";
        }
        if (id == "minecraft:lava_bucket" || id == "minecraft:powder_snow_bucket") {
            add(out, ItemCapability::BlockPlace); add(out, ItemCapability::ClutchRecovery);
            if (id == "minecraft:lava_bucket") add(out, ItemCapability::FireControl);
            out.activation = ItemActivationMode::Place; out.activations |= activation_mask(ItemActivationMode::Use);
            out.verified = true; out.confidence = 0.92;
            out.evidence = "known vanilla placeable bucket; use only where placement and resulting hazard are validated";
        }
        if (id == "minecraft:flint_and_steel" || id == "minecraft:fire_charge") {
            add(out, ItemCapability::FireControl); add(out, ItemCapability::ObjectiveDamage);
            out.activation = ItemActivationMode::Use; out.verified = true; out.confidence = 0.92;
            out.evidence = "known vanilla ignition item; effect is block/context-dependent";
        }
        if (id == "minecraft:shears") {
            add(out, ItemCapability::MiningTool); add(out, ItemCapability::WebEscape);
            out.activation = ItemActivationMode::Mine; out.activations |= activation_mask(ItemActivationMode::Attack);
            out.verified = true; out.confidence = 0.97;
            out.evidence = "known vanilla shears; can mine suitable blocks and web blocks";
        }
        if (id == "minecraft:bucket" || id == "minecraft:compass" || id == "minecraft:recovery_compass" ||
            id == "minecraft:clock" || id == "minecraft:map" || id == "minecraft:filled_map" ||
            id == "minecraft:spyglass" || id == "minecraft:ender_eye") {
            add(out, ItemCapability::ResourceUtility);
            out.activation = ItemActivationMode::Use; out.verified = true; out.confidence = 0.90;
            out.evidence = "known vanilla utility item; effect depends on world state";
        }
        if (id == "minecraft:chest" || id == "minecraft:ender_chest" || id == "minecraft:crafting_table" ||
            id == "minecraft:furnace" || id == "minecraft:blast_furnace" || id == "minecraft:smoker" ||
            id == "minecraft:anvil" || id == "minecraft:enchanting_table" || id == "minecraft:brewing_stand" ||
            id == "minecraft:smithing_table" || id == "minecraft:loom" || id == "minecraft:cartography_table" ||
            id == "minecraft:stonecutter" || id == "minecraft:grindstone") {
            add(out, ItemCapability::ResourceUtility); add(out, ItemCapability::BlockPlace);
            out.activation = ItemActivationMode::Place; out.activations |= activation_mask(ItemActivationMode::Interact);
            out.verified = true; out.confidence = 0.92;
            out.evidence = "known vanilla utility block; placement and interaction are separate actions";
        }
        if (id.size() > 8U && (id.find("_helmet") != std::string::npos || id.find("_chestplate") != std::string::npos ||
            id.find("_leggings") != std::string::npos || id.find("_boots") != std::string::npos)) {
            add(out, ItemCapability::Defence);
            out.activation = ItemActivationMode::Interact; out.verified = true; out.confidence = 0.94;
            out.evidence = "known vanilla wearable armour; equip action is handled by equipment management";
        }
        if (id.find("_ingot") != std::string::npos || id.find("_nugget") != std::string::npos ||
            id.find("_ore") != std::string::npos || id.find("_raw_iron") != std::string::npos ||
            id.find("_raw_gold") != std::string::npos || id.find("_raw_copper") != std::string::npos ||
            id.find("_gem") != std::string::npos || id.find("_shard") != std::string::npos ||
            id.find("_crystal") != std::string::npos || id == "minecraft:diamond" || id == "minecraft:emerald" ||
            id == "minecraft:coal" || id == "minecraft:lapis_lazuli" || id == "minecraft:redstone") {
            add(out, ItemCapability::ResourceUtility);
            if (out.activation == ItemActivationMode::Unknown) out.activation = ItemActivationMode::Interact;
            out.verified = true; out.confidence = std::max(out.confidence, 0.90);
            out.evidence = "known vanilla crafting/mining resource; item effect is inventory/economy-related";
        }
        if (id == "minecraft:ender_pearl") {
            add(out, ItemCapability::Teleport); add(out, ItemCapability::Throwable);
            add(out, ItemCapability::ClutchRecovery); add(out, ItemCapability::Mobility);
            out.activation = ItemActivationMode::Throw; out.verified = true; out.confidence = 0.99;
            out.evidence = "known vanilla ender pearl identifier";
        }
        if (id == "minecraft:wind_charge") {
            add(out, ItemCapability::Knockback); add(out, ItemCapability::BurstMovement);
            add(out, ItemCapability::Throwable); add(out, ItemCapability::ClutchRecovery);
            out.activation = ItemActivationMode::Throw; out.verified = true; out.confidence = 0.98;
            out.evidence = "known vanilla wind-charge identifier; trajectory still requires calibration";
        }
        if (id == "minecraft:fire_charge" || id == "minecraft:firework_rocket") {
            add(out, ItemCapability::Projectile); add(out, ItemCapability::Throwable); add(out, ItemCapability::Explosive);
            out.activation = ItemActivationMode::Use; out.verified = true; out.confidence = 0.92;
            out.evidence = "known vanilla projectile/charge identifier; mode-specific effect must be measured";
        }
        if (id == "minecraft:water_bucket") {
            add(out, ItemCapability::FireControl); add(out, ItemCapability::ClutchRecovery); add(out, ItemCapability::BlockPlace);
            out.activation = ItemActivationMode::Place; out.activations |= activation_mask(ItemActivationMode::Use);
            out.verified = true; out.confidence = 0.98;
            out.evidence = "known vanilla water bucket identifier; place/scoop interaction remains state and dimension dependent";
        }
        if (id == "minecraft:golden_apple" || id == "minecraft:enchanted_golden_apple") {
            add(out, ItemCapability::Food); add(out, ItemCapability::Healing);
            out.activation = ItemActivationMode::Consume; out.verified = true; out.confidence = 0.99;
            out.evidence = "known vanilla golden apple identifier";
        }
        if (id == "minecraft:potion" || id == "minecraft:splash_potion" || id == "minecraft:lingering_potion") {
            add(out, ItemCapability::Drinkable); add(out, ItemCapability::Throwable);
            out.activation = id == "minecraft:potion" ? ItemActivationMode::Consume : ItemActivationMode::Throw;
            out.verified = true; out.confidence = 0.85;
            const std::string variant = lower_ascii(descriptor.variant_key);
            if (variant.find("healing") != std::string::npos || variant.find("regeneration") != std::string::npos ||
                name.find("healing") != std::string::npos || name.find("regeneration") != std::string::npos) {
                add(out, ItemCapability::Healing);
                out.evidence = "potion container and healing/regeneration effect are identified";
            } else {
                out.evidence = "known potion container action; no healing effect inferred without decoded potion metadata";
            }
        }
        if (id == "minecraft:cobweb") {
            add(out, ItemCapability::BlockPlace); add(out, ItemCapability::WebEscape);
            out.activation = ItemActivationMode::Place; out.verified = true; out.confidence = 0.90;
            out.evidence = "known vanilla cobweb identifier";
        }
        if (id == "minecraft:tnt" || id == "minecraft:fire_charge" || id == "minecraft:wind_charge") {
            add(out, ItemCapability::Explosive); add(out, ItemCapability::ObjectiveDamage);
        }
        static const char* const foods[] = {
            "minecraft:apple", "minecraft:baked_potato", "minecraft:beetroot", "minecraft:beetroot_soup",
            "minecraft:bread", "minecraft:carrot", "minecraft:chicken", "minecraft:cooked_beef",
            "minecraft:cooked_chicken", "minecraft:cooked_cod", "minecraft:cooked_mutton",
            "minecraft:cooked_porkchop", "minecraft:cooked_rabbit", "minecraft:cooked_salmon",
            "minecraft:cookie", "minecraft:dried_kelp", "minecraft:enchanted_golden_apple", "minecraft:golden_apple",
            "minecraft:glow_berries", "minecraft:melon_slice", "minecraft:mushroom_stew", "minecraft:poisonous_potato",
            "minecraft:potato", "minecraft:pumpkin_pie", "minecraft:beef", "minecraft:porkchop", "minecraft:mutton",
            "minecraft:rabbit", "minecraft:salmon", "minecraft:cod", "minecraft:rotten_flesh", "minecraft:spider_eye",
            "minecraft:sweet_berries", "minecraft:tropical_fish"
        };
        if (std::find_if(std::begin(foods), std::end(foods), [&id](const char* food) { return id == food; }) != std::end(foods)) {
            add(out, ItemCapability::Food);
            if (out.activation == ItemActivationMode::Unknown) out.activation = ItemActivationMode::Consume;
            out.verified = true;
            out.confidence = std::max(out.confidence, 0.94);
            out.evidence = "known vanilla food identifier";
        }
        const bool solid_block = id_has("_planks") || id_has("_wool") || id_has("_terracotta") ||
            id_has("_concrete") || id_has("_stone_brick") || id_has("_bricks") || id_has("_sandstone") ||
            id_has("_glass") || id_has("_quartz_block") || id_has("_prismarine") || id_has("_purpur") ||
            id_has("_log") || id_has("_wood") || id_has("_stem") || id_has("_hyphae") ||
            id == "minecraft:stone" || id == "minecraft:cobblestone" || id == "minecraft:deepslate" ||
            id == "minecraft:end_stone" || id == "minecraft:obsidian" || id == "minecraft:netherrack" ||
            id == "minecraft:dirt" || id == "minecraft:grass_block" || id == "minecraft:clay" ||
            id == "minecraft:mud" || id == "minecraft:packed_ice" || id == "minecraft:blue_ice" ||
            id == "minecraft:snow_block" || id == "minecraft:slime_block" || id == "minecraft:honey_block" ||
            id == "minecraft:bone_block" || id == "minecraft:coal_block" || id == "minecraft:iron_block" ||
            id == "minecraft:gold_block" || id == "minecraft:diamond_block" || id == "minecraft:emerald_block" ||
            id == "minecraft:ladder" || id == "minecraft:scaffolding";
        const bool general_placeable = solid_block || id_has("_stairs") || id_has("_slab") || id_has("_fence") ||
            id_has("_wall") || id_has("_door") || id_has("_trapdoor") || id_has("_button") ||
            id_has("_pressure_plate") || id_has("_torch") || id_has("_sign") || id_has("_sapling") ||
            id_has("_leaves") || id_has("_coral") || id == "minecraft:tnt" || id == "minecraft:bed";
        if (general_placeable) {
            add(out, ItemCapability::BlockPlace);
            if (solid_block) add(out, ItemCapability::BridgeUtility);
            if (out.activation == ItemActivationMode::Unknown) out.activation = ItemActivationMode::Place;
            out.verified = true; out.confidence = std::max(out.confidence, 0.92);
            out.evidence = solid_block ? "known vanilla solid placeable/bridge block identifier" :
                                         "known vanilla placeable item; not automatically treated as a bridge block";
        }
        if (id == "minecraft:poisonous_potato" || id == "minecraft:spider_eye" || id == "minecraft:rotten_flesh" ||
            id == "minecraft:raw_chicken" || id == "minecraft:pufferfish") add(out, ItemCapability::UnsafeConsumable);
    }

    // Text hints help triage newly encountered Hive/custom items, but are not enough to
    // execute an ability. A server profile or controlled probe must verify the action.
    const bool custom = !known_vanilla;
    if (custom || out.capabilities == 0U) {
        if (label_has("sword") || label_has("blade") || label_has("katana") || label_has("axe")) add(out, ItemCapability::MeleeWeapon);
        if (label_has("bow") || label_has("crossbow") || label_has("blaster") || label_has("launcher")) add(out, ItemCapability::RangedWeapon);
        if (label_has("food") || label_has("apple") || label_has("steak") || label_has("snack") || label_has("cookie")) add(out, ItemCapability::Food);
        if (label_has("heal") || label_has("health") || label_has("regen") || label_has("recovery")) add(out, ItemCapability::Healing);
        if (label_has("shield") || label_has("guard") || label_has("barrier")) add(out, ItemCapability::Defence);
        if (label_has("pearl") || label_has("teleport") || label_has("blink")) { add(out, ItemCapability::Teleport); add(out, ItemCapability::ClutchRecovery); }
        if (label_has("wind charge") || label_has("boombox") || label_has("knockback") || label_has("blast")) { add(out, ItemCapability::Knockback); add(out, ItemCapability::BurstMovement); }
        if (label_has("block") || label_has("bridge") || label_has("platform") || label_has("wall")) { add(out, ItemCapability::BlockPlace); add(out, ItemCapability::BridgeUtility); }
        if (label_has("clutch") || label_has("rescue") || label_has("save") || label_has("landing")) add(out, ItemCapability::ClutchRecovery);
        if (label_has("pickaxe") || label_has("drill") || label_has("miner")) add(out, ItemCapability::MiningTool);
        if (label_has("fire") || label_has("extinguish") || label_has("water")) add(out, ItemCapability::FireControl);
        if (label_has("web") || label_has("shear") || label_has("escape")) add(out, ItemCapability::WebEscape);
        if (label_has("bomb") || label_has("tnt") || label_has("explosive") || label_has("fireball")) add(out, ItemCapability::Explosive);
        if (label_has("throw") || label_has("snowball") || label_has("egg") || label_has("projectile")) add(out, ItemCapability::Throwable);
        if (label_has("potion") || label_has("elixir")) add(out, ItemCapability::Drinkable);
        if (out.capabilities != 0U && out.evidence.empty()) out.evidence = "unverified display-name hint; action is not authorized";
        out.verified = false;
        out.activation = ItemActivationMode::Unknown;
        out.confidence = out.capabilities == 0U ? 0.0 : 0.20;
    }

    if (out.activation != ItemActivationMode::Unknown) out.activations |= activation_mask(out.activation);

    for (const std::string& tag : descriptor.tags) {
        const std::string normalized = lower_ascii(tag);
        if (normalized == "minecraft:is_food" || normalized == "food") add(out, ItemCapability::Food);
        else if (normalized == "minecraft:throwable" || normalized == "throwable") add(out, ItemCapability::Throwable);
        else if (normalized == "minecraft:hand_equipped" || normalized == "weapon") add(out, ItemCapability::MeleeWeapon);
        else if (normalized == "placeable" || normalized == "block_place") add(out, ItemCapability::BlockPlace);
        // A tag establishes a candidate capability, not the semantics of an activation packet.
    }
    return out;
}

bool ItemKnowledgeBase::register_profile(const ItemKnowledge& profile, std::string* error) {
    const auto set_error = [error](const std::string& message) { if (error) *error = message; };
    constexpr ItemCapabilityMask kValidCapabilities = (1ULL << 23U) - 1ULL;
    if (profile.identifier.empty() || profile.identifier.size() > 256U || !safe_field(profile.identifier) ||
        !safe_field(profile.variant_key) || profile.variant_key.size() > 512U || !safe_field(profile.display_name) || profile.display_name.size() > 256U || profile.capabilities == 0U || (profile.capabilities & ~kValidCapabilities) != 0U ||
        static_cast<unsigned int>(profile.activation) > static_cast<unsigned int>(ItemActivationMode::Interact) ||
        !std::isfinite(profile.confidence) || profile.confidence < 0.0 || profile.confidence > 1.0 ||
        profile.observed_outcomes > 10000000U || profile.evidence.size() > 512U || !safe_field(profile.evidence) ||
        (profile.projectile_physics.has_value() && !valid_projectile(*profile.projectile_physics, false))) {
        set_error("invalid or unsafe item profile fields");
        return false;
    }
    ItemKnowledge normalized = profile;
    if (normalized.activations == 0U && normalized.activation != ItemActivationMode::Unknown) {
        normalized.activations = activation_mask(normalized.activation);
    }
    if (normalized.activations != 0U && normalized.activation == ItemActivationMode::Unknown) {
        for (unsigned int mode = 1U; mode <= static_cast<unsigned int>(ItemActivationMode::Interact); ++mode) {
            const auto candidate = static_cast<ItemActivationMode>(mode);
            if (has_activation(normalized.activations, candidate)) { normalized.activation = candidate; break; }
        }
    }
    if (normalized.verified && normalized.activation == ItemActivationMode::Unknown) {
        set_error("verified executable item profiles must specify at least one activation mode");
        return false;
    }
    constexpr ItemActivationMask kValidActivationMask = (1U << (static_cast<unsigned int>(ItemActivationMode::Interact) + 1U)) - 2U;
    if ((normalized.activations & ~kValidActivationMask) != 0U ||
        (normalized.verified && !has_activation(normalized.activations, normalized.activation))) {
        set_error("item profile has an invalid or inconsistent activation mask"); return false;
    }
    profiles_[profile_key(normalized.identifier, normalized.variant_key)] = normalized;
    return true;
}

ItemKnowledge ItemKnowledgeBase::resolve(const ItemDescriptor& descriptor) const {
    const auto it = profiles_.find(profile_key(descriptor.identifier, descriptor.variant_key));
    if (it != profiles_.end()) return it->second;
    // Exact variant profiles must not bleed into a differently-enchanted/customized item,
    // potion effect, or server-provided item component variant.
    return infer_item_knowledge(descriptor);
}

bool ItemKnowledgeBase::record_outcome(const std::string& identifier, bool success, std::string* error) {
    const auto set_error = [error](const std::string& message) { if (error) *error = message; };
    ItemKnowledge* selected = nullptr;
    for (auto& entry : profiles_) {
        if (entry.second.identifier != identifier) continue;
        if (selected != nullptr) { set_error("item identifier has multiple variants; provide its exact descriptor"); return false; }
        selected = &entry.second;
    }
    if (selected == nullptr) { set_error("cannot learn an outcome for an unregistered profile"); return false; }
    if (selected->observed_outcomes < 10000000U) ++selected->observed_outcomes;
    const double increment = success ? 0.03 : -0.05;
    selected->confidence = std::max(0.05, std::min(0.99, selected->confidence + increment));
    if (!success) selected->verified = false;
    return true;
}

bool ItemKnowledgeBase::record_outcome(const ItemDescriptor& descriptor, bool success, std::string* error) {
    const auto set_error = [error](const std::string& message) { if (error) *error = message; };
    const auto it = profiles_.find(profile_key(descriptor.identifier, descriptor.variant_key));
    if (it == profiles_.end()) { set_error("cannot learn an outcome for an unregistered exact item profile"); return false; }
    ItemKnowledge& profile = it->second;
    if (profile.observed_outcomes < 10000000U) ++profile.observed_outcomes;
    const double increment = success ? 0.03 : -0.05;
    profile.confidence = std::max(0.05, std::min(0.99, profile.confidence + increment));
    if (!success) profile.verified = false;
    return true;
}

bool ItemKnowledgeBase::save_tsv(const std::string& path, std::string* error) const {
    const auto set_error = [error](const std::string& message) { if (error) *error = message; };
    if (path.empty()) { set_error("item-knowledge path is empty"); return false; }
    const std::filesystem::path target(path), temporary(path + ".tmp");
    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) { set_error("cannot create item-knowledge directory: " + ec.message()); return false; }
    }
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) { set_error("cannot create item-knowledge temporary file"); return false; }
        out << "BEDROCKAI_ITEM_KNOWLEDGE_V4\n" << std::setprecision(17);
        std::vector<std::string> keys;
        keys.reserve(profiles_.size());
        for (const auto& entry : profiles_) keys.push_back(entry.first);
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys) {
            const ItemKnowledge& p = profiles_.at(key);
            if (!safe_field(p.identifier) || !safe_field(p.display_name) || !safe_field(p.evidence) || !safe_field(p.variant_key)) {
                set_error("profile contains a field unsafe for TSV storage"); out.close(); std::filesystem::remove(temporary, ec); return false;
            }
            const ProjectilePhysics physics = p.projectile_physics.value_or(ProjectilePhysics{});
            out << p.identifier << '\t' << p.variant_key << '\t' << p.display_name << '\t' << p.capabilities << '\t'
                << static_cast<unsigned int>(p.activation) << '\t' << p.activations << '\t' << (p.verified ? 1 : 0) << '\t'
                << p.confidence << '\t' << p.observed_outcomes << '\t' << (p.projectile_physics.has_value() ? 1 : 0) << '\t'
                << physics.launch_speed << '\t' << physics.gravity << '\t' << physics.drag_per_tick << '\t'
                << physics.tick_rate << '\t' << physics.max_flight_seconds << '\t' << physics.hit_tolerance << '\t'
                << (physics.calibrated ? 1 : 0) << '\t' << p.evidence << '\n';
        }
        out.flush();
        if (!out) { set_error("failed writing item-knowledge data"); out.close(); std::filesystem::remove(temporary, ec); return false; }
    }
    std::filesystem::permissions(temporary, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);
    if (ec) { set_error("cannot restrict item-knowledge permissions: " + ec.message()); std::filesystem::remove(temporary, ec); return false; }
    std::filesystem::rename(temporary, target, ec);
    if (ec) { set_error("cannot commit item-knowledge file: " + ec.message()); std::error_code cleanup; std::filesystem::remove(temporary, cleanup); return false; }
    return true;
}

bool ItemKnowledgeBase::load_tsv(const std::string& path, std::string* error) {
    const auto set_error = [error](const std::string& message) { if (error) *error = message; };
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes > 4U * 1024U * 1024U) { set_error(ec ? "cannot stat item-knowledge file" : "item-knowledge file exceeds size limit"); return false; }
    std::ifstream in(path);
    if (!in) { set_error("cannot open item-knowledge file"); return false; }
    std::string header;
    if (!std::getline(in, header) || (header != "BEDROCKAI_ITEM_KNOWLEDGE_V1" &&
        header != "BEDROCKAI_ITEM_KNOWLEDGE_V2" && header != "BEDROCKAI_ITEM_KNOWLEDGE_V3" &&
        header != "BEDROCKAI_ITEM_KNOWLEDGE_V4")) {
        set_error("unsupported item-knowledge header"); return false;
    }
    const bool legacy_v1 = header == "BEDROCKAI_ITEM_KNOWLEDGE_V1";
    const bool has_variant = !legacy_v1;
    const bool has_activation_mask = header == "BEDROCKAI_ITEM_KNOWLEDGE_V3" || header == "BEDROCKAI_ITEM_KNOWLEDGE_V4";
    const bool has_projectile = header == "BEDROCKAI_ITEM_KNOWLEDGE_V4";
    std::unordered_map<std::string, ItemKnowledge> parsed;
    std::string line;
    auto parse_number = [](const std::string& value, auto& output) {
        std::istringstream stream(value);
        if (!(stream >> output)) return false;
        std::string extra;
        return !(stream >> extra);
    };
    auto split_tabs = [](const std::string& value) {
        std::vector<std::string> cells;
        std::size_t begin = 0U;
        while (begin <= value.size()) {
            const std::size_t end = value.find('\t', begin);
            if (end == std::string::npos) { cells.push_back(value.substr(begin)); break; }
            cells.push_back(value.substr(begin, end - begin));
            begin = end + 1U;
        }
        return cells;
    };
    std::size_t row_count = 0U;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (++row_count > 100000U) { set_error("too many item-knowledge rows"); return false; }
        const std::vector<std::string> cells = split_tabs(line);
        const std::size_t expected = has_projectile ? 18U : (has_activation_mask ? 10U : (has_variant ? 9U : 8U));
        if (cells.size() != expected) { set_error("malformed item-knowledge row/field count"); return false; }
        ItemKnowledge p;
        std::size_t i = 0U;
        p.identifier = cells[i++];
        if (has_variant) p.variant_key = cells[i++];
        p.display_name = cells[i++];
        unsigned long long caps = 0ULL;
        unsigned int activation = 0U;
        if (!parse_number(cells[i++], caps) || !parse_number(cells[i++], activation)) {
            set_error("invalid item capability or activation field"); return false;
        }
        unsigned int activations = 0U;
        if (has_activation_mask && !parse_number(cells[i++], activations)) {
            set_error("invalid item activation mask"); return false;
        }
        int verified = 0;
        if (!parse_number(cells[i++], verified) || !parse_number(cells[i++], p.confidence) ||
            !parse_number(cells[i++], p.observed_outcomes)) {
            set_error("invalid item verification, confidence, or outcome field"); return false;
        }
        int has_physics = 0;
        ProjectilePhysics physics{};
        if (has_projectile) {
            int calibrated = 0;
            if (!parse_number(cells[i++], has_physics) || !parse_number(cells[i++], physics.launch_speed) ||
                !parse_number(cells[i++], physics.gravity) || !parse_number(cells[i++], physics.drag_per_tick) ||
                !parse_number(cells[i++], physics.tick_rate) || !parse_number(cells[i++], physics.max_flight_seconds) ||
                !parse_number(cells[i++], physics.hit_tolerance) || !parse_number(cells[i++], calibrated) ||
                (has_physics != 0 && has_physics != 1) || (calibrated != 0 && calibrated != 1)) {
                set_error("invalid projectile calibration fields"); return false;
            }
            physics.calibrated = calibrated == 1;
            if (has_physics == 1) {
                if (!valid_projectile(physics, false)) { set_error("projectile profile contains non-finite physics"); return false; }
                p.projectile_physics = physics;
            }
        }
        p.evidence = cells[i++];
        const auto mode = parse_activation(activation);
        p.capabilities = static_cast<ItemCapabilityMask>(caps);
        p.activation = mode.value_or(ItemActivationMode::Unknown);
        p.activations = has_activation_mask ? activations : activation_mask(p.activation);
        p.verified = verified == 1;
        constexpr ItemActivationMask kValidActivationMask = (1U << (static_cast<unsigned int>(ItemActivationMode::Interact) + 1U)) - 2U;
        constexpr ItemCapabilityMask kValidCapabilities = (1ULL << 23U) - 1ULL;
        if (p.identifier.empty() || p.identifier.size() > 256U || !safe_field(p.identifier) ||
            !safe_field(p.variant_key) || p.variant_key.size() > 512U || !safe_field(p.display_name) ||
            p.display_name.size() > 256U || !mode || p.capabilities == 0U || (p.capabilities & ~kValidCapabilities) != 0U ||
            (p.activations & ~kValidActivationMask) != 0U || !std::isfinite(p.confidence) ||
            p.confidence < 0.0 || p.confidence > 1.0 || (verified != 0 && verified != 1) ||
            p.observed_outcomes > 10000000U || !safe_field(p.evidence) || p.evidence.size() > 512U ||
            (p.verified && (p.activation == ItemActivationMode::Unknown || p.confidence < 0.5 ||
                            !has_activation(p.activations, p.activation)))) {
            set_error("item-knowledge row failed validation"); return false;
        }
        // V1 profiles had no variant identity. Revoke custom profiles that may have been
        // attached to a renamed/version-specific item without enough identity information.
        if (legacy_v1 && p.identifier.rfind("minecraft:", 0U) != 0U) {
            p.verified = false;
            p.activation = ItemActivationMode::Unknown;
            p.activations = 0U;
            p.evidence = "legacy profile had no exact item variant identity; re-verify before use";
        }
        parsed[profile_key(p.identifier, p.variant_key)] = p;
    }
    if (!in.eof()) { set_error("failed reading item-knowledge data"); return false; }
    profiles_.swap(parsed);
    return true;
}

std::size_t ItemKnowledgeBase::size() const { return profiles_.size(); }

} // namespace bedrockai
