#include "core/gameplay_brain.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

using namespace bedrockai;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++failures; } } while (0)

static GameplayObservation basic_observation() {
    GameplayObservation observation;
    observation.world.joined = true;
    observation.world.tick = 100;
    observation.world.player.runtime_id = 5;
    observation.world.player.position = {0.0, 64.0, 0.0};
    observation.world.player.health = 20.0;
    observation.world.player.on_ground = true;
    observation.world.player.selected_slot = 0;
    observation.conditions.maximum_health = 20.0;
    return observation;
}

static EntityState hostile(std::uint64_t id, Vec3 position, double health = 20.0) {
    EntityState entity;
    entity.runtime_id = id;
    entity.type_id = 63;
    entity.position = position;
    entity.health = health;
    entity.hostile = true;
    entity.alive = true;
    return entity;
}

static InventoryStackObservation item(ItemKind kind, int slot, int count = 1) {
    InventoryStackObservation stack;
    stack.kind = kind;
    stack.slot = slot;
    stack.count = count;
    stack.ready = true;
    stack.durability_fraction = 1.0;
    return stack;
}

static ProjectilePhysics flat_physics(double speed = 20.0) {
    ProjectilePhysics physics;
    physics.launch_speed = speed;
    physics.gravity = 0.0;
    physics.drag_per_tick = 1.0;
    physics.tick_rate = 20.0;
    physics.max_flight_seconds = 3.0;
    physics.hit_tolerance = 0.5;
    physics.calibrated = true;
    return physics;
}

static void test_catalog_has_broad_domains() {
    const auto& catalog = gameplay_skill_catalog();
    CHECK(catalog.size() >= 30U, "catalog includes a broad prebuilt gameplay skill set");
    bool bow = false, clutch = false, bridge = false, bedwars = false, mace = false, web = false;
    for (const auto& skill : catalog) {
        bow = bow || skill.id == SkillId::BowLeadShot;
        clutch = clutch || skill.id == SkillId::WindChargeClutch || skill.id == SkillId::PearlEscape;
        bridge = bridge || skill.id == SkillId::DiagonalBridge;
        bedwars = bedwars || skill.id == SkillId::BedWarsRush;
        mace = mace || skill.id == SkillId::MaceSmash;
        web = web || skill.id == SkillId::EscapeWeb;
    }
    CHECK(bow && clutch && bridge && bedwars && mace && web,
          "catalog exposes ranged, clutch, bridge, BedWars, mace and web skill families");
}

static void test_joined_state_required() {
    GameplayObservation observation;
    const GameplayDecision decision = GameplayBrain{}.decide(observation);
    CHECK(!decision.valid, "brain refuses autonomous decisions before a joined finite world state exists");
    CHECK(decision.action == BrainAction::Observe, "unjoined state requests observation only");
}

static void test_food_decision_and_combat_deferral() {
    GameplayObservation observation = basic_observation();
    observation.conditions.hunger_known = true;
    observation.conditions.hunger = 5.0;
    auto food = item(ItemKind::Food, 3, 5);
    food.hunger_restore = 6.0;
    food.saturation_restore = 7.0;
    observation.inventory.push_back(food);
    GameplayDecision decision = GameplayBrain{}.decide(observation);
    CHECK(decision.valid && decision.action == BrainAction::EatFood, "low hunger and no threat selects ordinary food");
    CHECK(decision.item_slot.has_value() && *decision.item_slot == 3, "food decision identifies the slot to use");

    observation.world.world.entities.emplace(9U, hostile(9U, {1.0, 64.0, 1.0}));
    observation.conditions.immediate_threat = true;
    decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action != BrainAction::EatFood, "normal food is deferred when a hostile is observed");
}

static void test_void_recovery_requires_validated_capability() {
    GameplayObservation observation = basic_observation();
    observation.world.player.airborne = true;
    observation.world.player.velocity.y = -8.0;
    observation.conditions.falling_toward_void = true;
    observation.inventory.push_back(item(ItemKind::WindCharge, 4));
    observation.conditions.wind_charge_recovery_calibrated = false;
    GameplayDecision decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action == BrainAction::NoSafeRecoveryAction,
          "inventory item alone is insufficient to trigger an uncalibrated void-recovery technique");

    observation.conditions.wind_charge_recovery_calibrated = true;
    decision = GameplayBrain{}.decide(observation);
    CHECK(decision.valid && decision.emergency && decision.action == BrainAction::UseWindCharge,
          "known void fall selects the calibrated wind-charge clutch and marks it emergency");
    CHECK(decision.item_slot.has_value() && *decision.item_slot == 4,
          "wind-charge recovery identifies the exact inventory slot");

    observation.conditions.block_clutch_site_known = true;
    observation.conditions.block_placement_allowed = true;
    observation.inventory.push_back(item(ItemKind::BuildingBlock, 5, 32));
    SkillExperienceBook experience;
    for (int i = 0; i < 20; ++i) {
        experience.record(SkillId::WindChargeClutch, false, 0.0);
        experience.record(SkillId::BlockClutch, true, 1.0);
    }
    decision = GameplayBrain{}.decide(observation, &experience);
    CHECK(decision.action == BrainAction::BlockClutch,
          "observed outcome history changes recovery selection when a different validated skill performs better");
}

static void test_safe_heal_and_critical_retreat() {
    GameplayObservation observation = basic_observation();
    observation.world.player.health = 4.0;
    observation.conditions.safe_heal_window = true;
    observation.world.world.entities.emplace(9U, hostile(9U, {2.0, 64.0, 0.0}));
    observation.inventory.push_back(item(ItemKind::GoldenApple, 5));
    GameplayDecision decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action == BrainAction::EatGoldenApple,
          "critical health can heal when the environment explicitly marks a safe use window");

    observation.conditions.safe_heal_window = false;
    observation.conditions.immediate_threat = true;
    observation.inventory.clear();
    decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action == BrainAction::Retreat && decision.emergency,
          "critical health under attack without a validated escape or safe heal selects retreat");
}

static void test_melee_reach_fails_closed_then_attacks_when_calibrated() {
    GameplayObservation observation = basic_observation();
    observation.world.world.entities.emplace(9U, hostile(9U, {0.0, 64.0, 2.0}));
    observation.inventory.push_back(item(ItemKind::Sword, 0));
    GameplayDecision decision = GameplayBrain{}.decide(observation);
    CHECK(decision.valid && !decision.combat.propose_attack,
          "unmeasured reach never produces an attack proposal");

    observation.calibration.melee_reach = 3.0;
    decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action == BrainAction::Attack && decision.combat.propose_attack,
          "calibrated reach allows attack when the target is inside the validated range");
}

static void test_shield_and_axe_reactions() {
    GameplayObservation observation = basic_observation();
    observation.world.world.entities.emplace(9U, hostile(9U, {0.0, 64.0, 2.0}));
    observation.conditions.enemy_attacking = true;
    observation.inventory.push_back(item(ItemKind::Shield, 2));
    GameplayDecision decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action == BrainAction::RaiseShield && decision.item_slot == std::optional<int>(2),
          "observed enemy attack selects the available shield slot");

    observation.conditions.enemy_attacking = false;
    observation.conditions.target_using_shield_known = true;
    observation.conditions.target_using_shield = true;
    observation.world.player.selected_slot = 0;
    observation.inventory.push_back(item(ItemKind::Axe, 1));
    decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action != BrainAction::SwitchWeapon || decision.skill != SkillId::AxePunish,
          "vanilla Bedrock profile does not assume Java axe shield-disable mechanics");
    observation.conditions.server_axe_shield_punish_supported = true;
    decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action == BrainAction::SwitchWeapon && decision.skill == SkillId::AxePunish,
          "a measured custom-server capability can enable the axe counter-strategy");
    CHECK(decision.item_slot == std::optional<int>(1), "axe response identifies the correct hotbar slot");
}

static void test_projectile_solver_requires_calibration_and_leads_target() {
    ProjectileSolver solver;
    ProjectileRequest request;
    request.origin = {0.0, 0.0, 0.0};
    request.target_position = {0.0, 0.0, 20.0};
    ProjectilePhysics uncalibrated = flat_physics();
    uncalibrated.calibrated = false;
    CHECK(!solver.solve(uncalibrated, request).valid, "projectile solver refuses uncalibrated physics");

    const ProjectileSolution straight = solver.solve(flat_physics(), request);
    CHECK(straight.valid, "calibrated flat trajectory finds a direct solution");
    CHECK(std::abs(straight.yaw_degrees) < 0.2, "forward target produces near-zero yaw");
    CHECK(straight.miss_distance <= 0.5, "reported trajectory meets the configured miss tolerance");

    request.target_velocity.x = 4.0;
    request.latency_seconds = 0.1;
    const ProjectileSolution lead = solver.solve(flat_physics(25.0), request);
    CHECK(lead.valid, "moving target remains solvable with calibrated projectile speed");
    CHECK(lead.yaw_degrees < -1.0, "moving target is led horizontally rather than aiming at its old position");
}

static void test_bow_aiming_needs_los_ammunition_and_solution() {
    GameplayObservation observation = basic_observation();
    observation.world.world.entities.emplace(9U, hostile(9U, {0.0, 64.0, 20.0}));
    observation.conditions.line_of_sight_known = true;
    observation.conditions.line_of_sight = true;
    observation.inventory.push_back(item(ItemKind::Bow, 1));
    observation.inventory.push_back(item(ItemKind::Arrow, 2, 12));
    observation.calibration.arrow = flat_physics(30.0);
    GameplayDecision decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action == BrainAction::AimAndShootBow && decision.projectile.has_value() && decision.projectile->valid,
          "visible target, arrows and calibrated physics activate the bow aiming skill");
    CHECK(decision.item_slot == std::optional<int>(1), "ranged choice identifies bow slot");

    observation.conditions.line_of_sight = false;
    decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action != BrainAction::AimAndShootBow, "bow shot is withheld when line of sight is false");
}

static void test_experience_is_adaptive_and_persisted_without_credentials() {
    SkillExperienceBook book;
    CHECK(std::abs(book.effectiveness(SkillId::PearlEscape) - 0.5) < 1e-9,
          "unseen skill starts with neutral smoothed effectiveness");
    for (int i = 0; i < 10; ++i) book.record(SkillId::PearlEscape, true, 1.0);
    CHECK(book.effectiveness(SkillId::PearlEscape) > 0.85,
          "repeated successful outcomes raise subsequent skill effectiveness");
    for (int i = 0; i < 8; ++i) book.record(SkillId::BlockClutch, false, 0.0);
    CHECK(book.effectiveness(SkillId::BlockClutch) < 0.2,
          "repeated failures lower subsequent skill effectiveness");

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "bedrockai_skill_memory_v28.tsv";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path.string() + ".tmp", ec);
    std::string error;
    CHECK(book.save_tsv(path.string(), &error), "experience book saves atomically");
    SkillExperienceBook restored;
    CHECK(restored.load_tsv(path.string(), &error), "experience book reloads into a clean object");
    CHECK(restored.get(SkillId::PearlEscape).attempts == 10U,
          "persisted data retains attempt counts for the skill learner");
    CHECK(restored.get(SkillId::BlockClutch).successes == 0U,
          "persisted data retains failed outcomes");
    const auto permissions = std::filesystem::status(path, ec).permissions();
    CHECK(!ec && (permissions & std::filesystem::perms::group_read) == std::filesystem::perms::none &&
          (permissions & std::filesystem::perms::others_read) == std::filesystem::perms::none,
          "experience data file is not group/world readable");
    std::filesystem::remove(path, ec);
}

static void test_objective_skills_use_live_inputs() {
    GameplayObservation observation = basic_observation();
    observation.goal = GameplayGoal::Mine;
    observation.conditions.objective_block_known = true;
    observation.conditions.objective_block_x = 10;
    observation.conditions.objective_block_y = 63;
    observation.conditions.objective_block_z = 3;
    observation.inventory.push_back(item(ItemKind::Pickaxe, 2));
    GameplayDecision decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action == BrainAction::MineBlock && decision.item_slot == std::optional<int>(2),
          "mining goal selects the observed pickaxe and known target block");
    CHECK(decision.target_position.has_value() && std::abs(decision.target_position->x - 10.5) < 1e-9,
          "mining decision reports the centre of the target block");

    observation.goal = GameplayGoal::Bridge;
    observation.conditions.objective_target_known = true;
    observation.conditions.objective_target = {8.0, 64.0, 4.0};
    observation.inventory.push_back(item(ItemKind::BuildingBlock, 3, 32));
    decision = GameplayBrain{}.decide(observation);
    CHECK(decision.action == BrainAction::Bridge && decision.item_slot == std::optional<int>(3),
          "bridge objective selects building blocks and target waypoint");
}

int main() {
    test_catalog_has_broad_domains();
    test_joined_state_required();
    test_food_decision_and_combat_deferral();
    test_void_recovery_requires_validated_capability();
    test_safe_heal_and_critical_retreat();
    test_melee_reach_fails_closed_then_attacks_when_calibrated();
    test_shield_and_axe_reactions();
    test_projectile_solver_requires_calibration_and_leads_target();
    test_bow_aiming_needs_los_ammunition_and_solution();
    test_experience_is_adaptive_and_persisted_without_credentials();
    test_objective_skills_use_live_inputs();
    if (failures == 0) std::cout << "All v28 gameplay-brain tests passed\n";
    return failures == 0 ? 0 : 1;
}
