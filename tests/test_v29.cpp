#include "core/gameplay_brain.hpp"
#include "core/gameplay_control_loop.hpp"
#include "core/item_knowledge.hpp"
#include "core/server_rules.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>

using namespace bedrockai;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++failures; } } while (0)

static GameplayObservation base_observation() {
    GameplayObservation o;
    o.world.joined = true;
    o.world.tick = 100U;
    o.world.player.runtime_id = 5U;
    o.world.player.position = {0.0, 64.0, 0.0};
    o.world.player.health = 20.0;
    o.world.player.on_ground = true;
    o.world.player.selected_slot = 0;
    o.conditions.maximum_health = 20.0;
    return o;
}

static EntityState enemy(std::uint64_t id, Vec3 p, double health = 20.0) {
    EntityState e;
    e.runtime_id = id;
    e.position = p;
    e.health = health;
    e.hostile = true;
    e.alive = true;
    return e;
}

static InventoryStackObservation known_item(ItemKind kind, int slot) {
    InventoryStackObservation s;
    s.kind = kind;
    s.slot = slot;
    s.count = 1;
    s.ready = true;
    s.durability_fraction = 1.0;
    return s;
}

static AttackTrial trial(std::uint32_t interval, double damage, bool accepted) {
    AttackTrial t;
    t.interval_ticks = interval;
    t.normalized_damage = damage;
    t.accepted = accepted;
    t.target_vulnerability_known = true;
    t.target_vulnerable = true;
    t.confounders_controlled = true;
    return t;
}

static void test_combat_profile_learns_spam_friendly() {
    ServerRulesLearner learner;
    for (int i = 0; i < 5; ++i) learner.observe_attack_trial(trial(2U, 0.95, true));
    const auto p = learner.profile();
    CHECK(p.attack_model == AttackModel::SpamFriendly, "repeated short-interval full-damage outcomes identify spam-friendly combat");
    CHECK(p.confidence >= 0.70, "consistent evidence increases ruleset confidence");
    CHECK(p.full_damage_interval_ticks.has_value(), "full-damage intervals are retained as an empirical measure");
}

static void test_combat_profile_learns_java_style_cooldown_gate() {
    ServerRulesLearner learner;
    for (int i = 0; i < 4; ++i) learner.observe_attack_trial(trial(2U, 0.15, true));
    for (int i = 0; i < 5; ++i) learner.observe_attack_trial(trial(12U, 0.95, true));
    const auto p = learner.profile();
    CHECK(p.attack_model == AttackModel::CooldownGated, "fast attacks with reduced damage and longer full-damage intervals identify cooldown gating");
    CHECK(p.full_damage_interval_ticks.has_value() && *p.full_damage_interval_ticks == 12U,
          "measured full-damage interval is made available to the live decision engine");
    CHECK(p.evidence_note.find("cooldown-gated") != std::string::npos, "profile explains the evidence behind the classification");
}

static void test_invalid_or_confounded_trials_do_not_train() {
    ServerRulesLearner learner;
    AttackTrial invalid = trial(2U, 1.0, true);
    invalid.confounders_controlled = false;
    for (int i = 0; i < 10; ++i) learner.observe_attack_trial(invalid);
    CHECK(learner.profile().attack_model == AttackModel::Unknown && learner.profile().valid_trials == 0U,
          "armour, invulnerability, unknown vulnerability or other confounders must not be mistaken for combat rules");
    learner.observe_server_family_hint(ServerFamilyHint::JavaViaBedrockProxy, false);
    CHECK(learner.profile().family_hint == ServerFamilyHint::Unknown, "server family is not guessed from unsupported hints");
    learner.observe_server_family_hint(ServerFamilyHint::JavaViaBedrockProxy, true);
    CHECK(learner.profile().family_hint == ServerFamilyHint::JavaViaBedrockProxy, "explicitly grounded server family hint can be recorded");
}

static void test_features_require_verified_results() {
    ServerRulesLearner learner;
    learner.observe_feature(ServerFeature::AxeShieldPunish, true, false);
    CHECK(!learner.profile().axe_shield_punish_supported, "a guessed Java mechanic cannot be enabled on Bedrock");
    learner.observe_feature(ServerFeature::AxeShieldPunish, true, true);
    CHECK(learner.profile().axe_shield_punish_supported, "confirmed custom-server behaviour becomes a profile capability");
}

static void test_item_inference_separates_hints_from_executable_profiles() {
    ItemKnowledge pearl = infer_item_knowledge({"minecraft:ender_pearl", "Ender Pearl", {}, {}});
    CHECK(pearl.verified && pearl.activation == ItemActivationMode::Throw &&
          has_capability(pearl.capabilities, ItemCapability::Teleport),
          "known vanilla pearl provides a verified conventional use profile");

    ItemKnowledge hive_custom = infer_item_knowledge({"hive:rescue_platform", "Rescue Platform", {}, {}});
    CHECK(has_capability(hive_custom.capabilities, ItemCapability::ClutchRecovery) &&
          has_capability(hive_custom.capabilities, ItemCapability::BlockPlace),
          "unknown Hive item labels create useful candidate capabilities");
    CHECK(!hive_custom.verified && hive_custom.activation == ItemActivationMode::Unknown,
          "a name guess never authorizes a custom item action or assumes its exact effect");
}

static ItemKnowledge verified_hive_profile() {
    ItemKnowledge p;
    p.identifier = "hive:rescue_platform";
    p.display_name = "Rescue Platform";
    p.capabilities = capability_mask(ItemCapability::ClutchRecovery) | capability_mask(ItemCapability::BlockPlace);
    p.activation = ItemActivationMode::Use;
    p.verified = true;
    p.confidence = 0.95;
    p.observed_outcomes = 7U;
    p.evidence = "verified in controlled Hive practice session";
    return p;
}

static void test_item_profiles_register_persist_and_revoke_after_failure() {
    ItemKnowledgeBase knowledge;
    std::string error;
    CHECK(knowledge.register_profile(verified_hive_profile(), &error), "validated server-specific item profile registers");
    ItemKnowledge launcher;
    launcher.identifier = "hive:tested_blaster";
    launcher.variant_key = "ability=projectile";
    launcher.display_name = "Tested Blaster";
    launcher.capabilities = capability_mask(ItemCapability::RangedWeapon) | capability_mask(ItemCapability::Projectile);
    launcher.activation = ItemActivationMode::Use;
    launcher.activations = activation_mask(ItemActivationMode::Use) | activation_mask(ItemActivationMode::Throw);
    launcher.verified = true;
    launcher.confidence = 0.94;
    launcher.evidence = "server-specific launch and impact confirmed in controlled practice";
    ProjectilePhysics calibrated;
    calibrated.launch_speed = 20.0;
    calibrated.gravity = 0.0;
    calibrated.drag_per_tick = 1.0;
    calibrated.tick_rate = 20.0;
    calibrated.max_flight_seconds = 3.0;
    calibrated.hit_tolerance = 0.5;
    calibrated.calibrated = true;
    launcher.projectile_physics = calibrated;
    CHECK(knowledge.register_profile(launcher, &error), "server-specific projectile profile records physics and multiple activations");
    const auto resolved = knowledge.resolve({"hive:rescue_platform", "Rescue Platform", {}, {}});
    CHECK(resolved.verified && resolved.activation == ItemActivationMode::Use, "exact item ID resolves to its server-tested profile");
    CHECK(knowledge.record_outcome("hive:rescue_platform", true, &error), "item outcome is recorded");

    const auto path = std::filesystem::temp_directory_path() / "bedrockai_item_knowledge_v29.tsv";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path.string() + ".tmp", ec);
    CHECK(knowledge.save_tsv(path.string(), &error), "item profiles persist atomically");
    ItemKnowledgeBase restored;
    CHECK(restored.load_tsv(path.string(), &error), "item profiles reload into a clean knowledge base");
    CHECK(restored.resolve({"hive:rescue_platform", "Rescue Platform", {}, {}}).observed_outcomes == 8U,
          "outcome history persists across sessions");
    const auto restored_launcher = restored.resolve({"hive:tested_blaster", "Tested Blaster", {}, "ability=projectile"});
    CHECK(restored_launcher.projectile_physics.has_value() && restored_launcher.projectile_physics->calibrated &&
          has_activation(restored_launcher.activations, ItemActivationMode::Throw) &&
          has_activation(restored_launcher.activations, ItemActivationMode::Use),
          "variant identity, multi-action support and calibrated projectile physics persist together");
    CHECK(knowledge.record_outcome("hive:rescue_platform", false, &error), "failed item result is recorded");
    CHECK(!knowledge.resolve({"hive:rescue_platform", "Rescue Platform", {}, {}}).verified,
          "a failed activation disables unsafe automatic reuse until the profile is re-verified");
    const auto perms = std::filesystem::status(path, ec).permissions();
    CHECK(!ec && (perms & std::filesystem::perms::group_read) == std::filesystem::perms::none &&
          (perms & std::filesystem::perms::others_read) == std::filesystem::perms::none,
          "item-learning history is stored as owner-only data");
    std::filesystem::remove(path, ec);
}

static void test_brain_uses_verified_hive_item_for_emergency() {
    GameplayObservation o = base_observation();
    o.world.player.airborne = true;
    o.world.player.velocity = {0.0, -8.0, 0.0};
    o.conditions.falling_toward_void = true;
    InventoryStackObservation rescue = known_item(ItemKind::Unknown, 4);
    rescue.identifier = "hive:rescue_platform";
    rescue.display_name = "Rescue Platform";
    rescue.capabilities = capability_mask(ItemCapability::ClutchRecovery) | capability_mask(ItemCapability::BlockPlace);
    rescue.activation = ItemActivationMode::Use;
    rescue.capability_profile_verified = true;
    rescue.profile_confidence = 0.95;
    o.inventory.push_back(rescue);
    const GameplayDecision d = GameplayBrain{}.decide(o);
    CHECK(d.valid && d.emergency && d.action == BrainAction::UseAdaptiveItem,
          "emergency planner can use a registered Hive/custom recovery item when known vanilla recoveries are absent");
    CHECK(d.item_slot == 4 && d.item_identifier == std::optional<std::string>("hive:rescue_platform"),
          "custom-item decision carries the exact slot and item identity to the action executor");
}

static void test_brain_prefers_verified_custom_weapon_when_learned_better() {
    GameplayObservation o = base_observation();
    o.world.world.entities.emplace(9U, enemy(9U, {0.0, 64.0, 2.0}));
    o.inventory.push_back(known_item(ItemKind::Sword, 1));
    InventoryStackObservation weapon = known_item(ItemKind::Unknown, 3);
    weapon.identifier = "friends_smp:charged_blade";
    weapon.display_name = "Charged Blade";
    weapon.capabilities = capability_mask(ItemCapability::MeleeWeapon) | capability_mask(ItemCapability::Knockback);
    weapon.activation = ItemActivationMode::Attack;
    weapon.capability_profile_verified = true;
    weapon.profile_confidence = 0.95;
    o.inventory.push_back(weapon);
    const GameplayDecision d = GameplayBrain{}.decide(o);
    CHECK(d.action == BrainAction::SwitchWeapon && d.item_slot == 3,
          "weapon choice can adapt to a verified server-specific item instead of always selecting a sword");
    CHECK(d.item_identifier == std::optional<std::string>("friends_smp:charged_blade"),
          "weapon switching preserves custom item identity");
}

static void test_attack_cadence_profile_prevents_spam_on_cooldown_server() {
    GameplayObservation o = base_observation();
    o.world.tick = 104U;
    o.world.server_tick = 104U;
    o.world.server_tick_known = true;
    o.world.player.last_attack_tick = 100U;
    o.world.world.entities.emplace(9U, enemy(9U, {0.0, 64.0, 2.0}));
    o.inventory.push_back(known_item(ItemKind::Sword, 0));
    o.calibration.melee_reach = 3.0;
    o.server_mechanics.attack_model = AttackModel::CooldownGated;
    o.server_mechanics.full_damage_interval_ticks = 12U;
    GameplayDecision d = GameplayBrain{}.decide(o);
    CHECK(d.action != BrainAction::Attack && !d.combat.propose_attack,
          "controller withholds a repeat attack before the server-measured full-damage interval");
    CHECK(d.reason.find("cooldown-gated") != std::string::npos,
          "decision exposes why it is waiting rather than blindly spam-clicking");

    o.world.tick = 113U;
    o.world.server_tick = 113U;
    d = GameplayBrain{}.decide(o);
    CHECK(d.action == BrainAction::Attack && d.combat.propose_attack,
          "controller permits an attack once the learned interval has elapsed and reach is validated");
}

static void test_melee_style_changes_with_observed_success_history() {
    GameplayObservation o = base_observation();
    o.world.world.entities.emplace(9U, enemy(9U, {0.0, 64.0, 2.0}));
    o.calibration.melee_reach = 3.0;
    SkillExperienceBook history;
    for (int i = 0; i < 20; ++i) history.record(SkillId::ChooseMeleeStyle, true, 1.0);
    for (int i = 0; i < 20; ++i) history.record(SkillId::SwordStrafeCombo, false, 0.0);
    const GameplayDecision d = GameplayBrain{}.decide(o, &history);
    CHECK(d.combat_style == CombatStyle::Aggressive,
          "learned outcomes can move the choice away from the default strafe-oriented prior");
}

class FakeObservationSource final : public GameplayObservationSource {
public:
    explicit FakeObservationSource(GameplayObservation observation) : observation_(std::move(observation)) {}
    bool read(GameplayObservation& output, std::string* error) override {
        if (!available_) { if (error) *error = "test source has no fresh tick"; return false; }
        output = observation_;
        return true;
    }
    GameplayObservation observation_;
    bool available_ = true;
};

class FakeActionSink final : public GameplayActionSink {
public:
    ActionSubmission submit(const GameplayDecision& decision, std::string* error) override {
        (void)error;
        last_decision = decision;
        ++submissions;
        return {true, true, true, 1.0, "test action accepted and outcome verified"};
    }
    GameplayDecision last_decision{};
    int submissions = 0;
};

static void test_observation_brain_action_loop() {
    GameplayObservation observation = base_observation();
    observation.server_profile_key = "friends-smp:survival";
    observation.world.world.entities.emplace(9U, enemy(9U, {0.0, 64.0, 8.0}));
    FakeObservationSource source(observation);
    FakeActionSink sink;
    GameplayControlLoop loop(source, sink);
    const GameplayTickResult tick = loop.tick();
    CHECK(tick.observation_ready && tick.decision.valid, "control loop obtains a real-shaped observation and runs the brain");
    CHECK(tick.action_attempted && tick.action_submitted && sink.submissions == 1,
          "valid non-wait decision is handed to the action-encoder interface");
    CHECK(loop.experience().get(tick.decision.skill).attempts == 1U &&
          loop.experience().get(tick.decision.skill).successes == 1U,
          "observed outcome, not merely packet submission, feeds the skill learner");
    CHECK(loop.active_profile_key() == "friends-smp:survival", "learning is keyed to the current server and mode profile");

    source.observation_.server_profile_key = "hive:treasure-wars";
    const GameplayTickResult hive_tick = loop.tick();
    CHECK(hive_tick.observation_ready && loop.active_profile_key() == "hive:treasure-wars",
          "switching from an SMP to a Hive mode loads a distinct learning profile");
    CHECK(loop.experience().get(tick.decision.skill).attempts == 1U,
          "server-specific experience is not mixed with another world's outcomes");

    source.available_ = false;
    const GameplayTickResult stale = loop.tick();
    CHECK(!stale.observation_ready && !stale.action_attempted && sink.submissions == 2,
          "missing fresh game state stops the loop rather than acting on stale observations");
}

static void test_multi_action_and_variant_specific_item_profiles() {
    const ItemKnowledge trident = infer_item_knowledge({"minecraft:trident", "Trident", {}, {}});
    CHECK(trident.verified && has_capability(trident.capabilities, ItemCapability::RangedWeapon) &&
          has_activation(trident.activations, ItemActivationMode::Attack) &&
          has_activation(trident.activations, ItemActivationMode::Throw),
          "a multifunction vanilla item exposes separate melee and thrown actions");

    const ItemKnowledge unidentified_potion = infer_item_knowledge({"minecraft:potion", "Potion", {}, {}});
    const ItemKnowledge healing_potion = infer_item_knowledge({"minecraft:potion", "Potion", {}, "effect=healing"});
    CHECK(!has_capability(unidentified_potion.capabilities, ItemCapability::Healing),
          "potion container identity alone does not claim its effect is healing");
    CHECK(has_capability(healing_potion.capabilities, ItemCapability::Healing),
          "decoded potion variant metadata can identify its healing effect");

    ItemKnowledgeBase knowledge;
    ItemKnowledge burst;
    burst.identifier = "hive:utility_item";
    burst.variant_key = "ability=burst";
    burst.display_name = "Utility Item";
    burst.capabilities = capability_mask(ItemCapability::BurstMovement) | capability_mask(ItemCapability::Throwable);
    burst.activation = ItemActivationMode::Throw;
    burst.verified = true;
    burst.confidence = 0.94;
    burst.evidence = "verified on Hive BedWars test profile";
    CHECK(knowledge.register_profile(burst), "variant-specific custom item profile registers");
    ItemKnowledge barrier = burst;
    barrier.variant_key = "ability=barrier";
    barrier.capabilities = capability_mask(ItemCapability::Defence);
    barrier.activation = ItemActivationMode::Use;
    barrier.activations = activation_mask(ItemActivationMode::Use);
    CHECK(knowledge.register_profile(barrier), "same identifier can have a distinct verified variant");
    CHECK(has_capability(knowledge.resolve({"hive:utility_item", "Utility Item", {}, "ability=burst"}).capabilities,
                         ItemCapability::BurstMovement), "burst variant resolves by exact metadata identity");
    const ItemKnowledge unresolved = knowledge.resolve({"hive:utility_item", "Utility Item", {}, "ability=unknown"});
    CHECK(!unresolved.verified && unresolved.activation == ItemActivationMode::Unknown,
          "an unrecognized variant cannot inherit another variant's executable behaviour");
    std::string error;
    CHECK(!knowledge.record_outcome("hive:utility_item", true, &error),
          "outcomes for several item variants must specify the exact variant");
}

static void test_server_rules_and_learning_auto_persist_per_profile() {
    const auto root = std::filesystem::temp_directory_path() / "bedrockai_v29_learning_profiles";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    GameplayObservation observation = base_observation();
    observation.server_profile_key = "hive:bedwars";
    observation.world.world.entities.emplace(9U, enemy(9U, {0.0, 64.0, 8.0}));
    FakeObservationSource source(observation);
    FakeActionSink sink;
    GameplayControlLoop loop(source, sink);
    std::string error;
    CHECK(loop.configure_learning_directory(root.string(), &error), "learning storage configures before the first tick");
    CHECK(!loop.configure_learning_directory((root / "late").string(), &error),
          "learning storage cannot silently change after profiles have loaded");
    const auto first_tick = loop.tick();
    CHECK(first_tick.observation_ready, "learning loop starts on a fresh Hive profile");
    for (int i = 0; i < 5; ++i) loop.observe_attack_trial(trial(2U, 0.96, true));
    CHECK(loop.learned_server_mechanics_for_active_profile().attack_model == AttackModel::SpamFriendly,
          "verified combat outcomes change the active profile's attack model");
    loop.observe_server_family_hint(ServerFamilyHint::JavaViaBedrockProxy, true);
    loop.observe_server_feature(ServerFeature::AxeShieldPunish, false, true);
    loop.observe_server_feature(ServerFeature::FireballUse, true, true);
    const auto learned_features = loop.learned_server_mechanics_for_active_profile();
    CHECK(learned_features.family_hint == ServerFamilyHint::JavaViaBedrockProxy &&
          (learned_features.verified_feature_mask & (1U << static_cast<unsigned int>(ServerFeature::AxeShieldPunish))) != 0U &&
          !learned_features.axe_shield_punish_supported && learned_features.fireball_use_supported,
          "explicitly verified negative and positive mechanics are kept separately from unknown defaults");

    ItemKnowledge gadget;
    gadget.identifier = "hive:impact_gadget";
    gadget.display_name = "Impact Gadget";
    gadget.capabilities = capability_mask(ItemCapability::Knockback) | capability_mask(ItemCapability::Throwable);
    gadget.activation = ItemActivationMode::Throw;
    gadget.verified = true;
    gadget.confidence = 0.92;
    gadget.evidence = "controlled practice test; effect and activation confirmed";
    CHECK(loop.register_item_profile("hive:bedwars", gadget, &error), "server-specific item profile registers and persists");
    CHECK(loop.record_item_outcome("hive:impact_gadget", true, &error), "item success is recorded in active profile");
    CHECK(loop.save_learning_state(&error), "rules, items and skill history are saved together");

    FakeObservationSource restored_source(observation);
    FakeActionSink restored_sink;
    GameplayControlLoop restored(restored_source, restored_sink);
    CHECK(restored.configure_learning_directory(root.string(), &error), "second controller configures the same state directory");
    (void)restored.tick();
    const ServerMechanicsProfile restored_rules = restored.learned_server_mechanics_for_active_profile();
    CHECK(restored_rules.attack_model == AttackModel::SpamFriendly,
          "server combat profile reloads after restart");
    CHECK(restored_rules.family_hint == ServerFamilyHint::JavaViaBedrockProxy &&
          (restored_rules.verified_feature_mask & (1U << static_cast<unsigned int>(ServerFeature::AxeShieldPunish))) != 0U &&
          !restored_rules.axe_shield_punish_supported && restored_rules.fireball_use_supported,
          "restart preserves verified negative and positive mechanics without confusing false with unknown");
    const ItemKnowledgeBase* restored_items = restored.item_knowledge_for_active_profile();
    CHECK(restored_items != nullptr && restored_items->resolve({"hive:impact_gadget", "Impact Gadget", {}, {}}).verified,
          "verified Hive-specific item profile reloads after restart");

    restored_source.observation_.server_profile_key = "friends-smp:survival";
    (void)restored.tick();
    CHECK(restored.learned_server_mechanics_for_active_profile().attack_model == AttackModel::Unknown,
          "a friend's SMP profile does not inherit Hive attack cadence");
    const ItemKnowledgeBase* smp_items = restored.item_knowledge_for_active_profile();
    CHECK(smp_items != nullptr && !smp_items->resolve({"hive:impact_gadget", "Impact Gadget", {}, {}}).verified,
          "a friend's SMP does not inherit Hive-only custom item behaviour");
    std::filesystem::remove_all(root, ec);
}

static void test_control_loop_resolves_custom_projectile_profile_before_planning() {
    GameplayObservation observation = base_observation();
    observation.server_profile_key = "hive:bedwars";
    observation.conditions.line_of_sight_known = true;
    observation.conditions.line_of_sight = true;
    observation.world.world.entities.emplace(77U, enemy(77U, {0.0, 64.0, 10.0}));
    InventoryStackObservation launcher = known_item(ItemKind::Unknown, 2);
    launcher.identifier = "hive:tested_blaster";
    launcher.display_name = "Tested Blaster";
    launcher.variant_key = "ability=projectile";
    observation.inventory.push_back(launcher);
    FakeObservationSource source(observation);
    FakeActionSink sink;
    GameplayControlLoop loop(source, sink);
    ItemKnowledge profile;
    profile.identifier = "hive:tested_blaster";
    profile.variant_key = "ability=projectile";
    profile.display_name = "Tested Blaster";
    profile.capabilities = capability_mask(ItemCapability::RangedWeapon) | capability_mask(ItemCapability::Projectile);
    profile.activation = ItemActivationMode::Use;
    profile.activations = activation_mask(ItemActivationMode::Use) | activation_mask(ItemActivationMode::Throw);
    profile.verified = true;
    profile.confidence = 0.94;
    profile.evidence = "controlled Hive practice calibration";
    ProjectilePhysics physics;
    physics.launch_speed = 20.0;
    physics.gravity = 0.0;
    physics.drag_per_tick = 1.0;
    physics.tick_rate = 20.0;
    physics.max_flight_seconds = 3.0;
    physics.hit_tolerance = 0.7;
    physics.calibrated = true;
    profile.projectile_physics = physics;
    std::string error;
    CHECK(loop.register_item_profile("hive:bedwars", profile, &error), "profile registers before first live decision");
    const GameplayTickResult tick = loop.tick();
    CHECK(tick.observation_ready, "control loop reads the fresh observation");
    CHECK(tick.decision.action == BrainAction::UseAdaptiveItem &&
          tick.decision.item_identifier == std::optional<std::string>("hive:tested_blaster"),
          "verified custom projectile is resolved into an executable adaptive decision");
    CHECK(tick.decision.item_activation == ItemActivationMode::Use && tick.decision.projectile.has_value() &&
          tick.decision.projectile->valid,
          "planner emits an exact activation and calibrated shot solution rather than an item-name guess");
}

int main() {
    test_combat_profile_learns_spam_friendly();
    test_combat_profile_learns_java_style_cooldown_gate();
    test_invalid_or_confounded_trials_do_not_train();
    test_features_require_verified_results();
    test_item_inference_separates_hints_from_executable_profiles();
    test_item_profiles_register_persist_and_revoke_after_failure();
    test_brain_uses_verified_hive_item_for_emergency();
    test_brain_prefers_verified_custom_weapon_when_learned_better();
    test_attack_cadence_profile_prevents_spam_on_cooldown_server();
    test_melee_style_changes_with_observed_success_history();
    test_observation_brain_action_loop();
    test_multi_action_and_variant_specific_item_profiles();
    test_server_rules_and_learning_auto_persist_per_profile();
    test_control_loop_resolves_custom_projectile_profile_before_planning();
    if (failures == 0) std::cout << "All v29 adaptive-rules and dynamic-item tests passed\n";
    return failures == 0 ? 0 : 1;
}
