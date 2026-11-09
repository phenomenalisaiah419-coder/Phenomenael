#include "sim_rig.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace bedrockai;
using namespace simrig;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAIL: " << #condition << " at line " << __LINE__ << "\n"; \
    std::exit(1); } } while (false)

namespace {

GuardianInput gin(const WorldGrid& g, Vec3 feet, double yaw = kFaceX) {
    GuardianInput in;
    in.grid = &g;
    in.feet = feet;
    in.yaw_deg = yaw;
    in.stick_forward = 1.0;
    in.sprint = true;
    return in;
}

// ------------------------------------------------------------ guardian (unit level)

void test_guardian_lead_and_halt() {
    WorldGrid g(-64, 319);
    g.mark_region_loaded(-48, -48, 96, 96);
    platform(g, 0, 0, 7, 7);
    SurvivalGuardian guard;
    // Far from the edge: a warning, but the human keeps full control.
    GuardianPlan far = guard.evaluate(gin(g, Vec3{3.5, 64.0, 3.5}));
    CHECK(!far.intervene && far.warning && far.hazard.found && far.hazard.kind == HazardKind::Void);
    // Close to the edge, nothing in the kit: halt + sneak overrides the stick.
    GuardianPlan near = guard.evaluate(gin(g, Vec3{7.5, 64.0, 3.5}));
    CHECK(near.intervene && near.action == GuardianAction::Halt && near.override_movement && near.sneak);
    CHECK(near.move_forward == 0.0 && near.technique_id == "survival.sneak_halt");
    // Same place with blocks: sneak-creep forward and bridge the missing cell.
    GuardianInput with_blocks = gin(g, Vec3{7.5, 64.0, 3.5});
    with_blocks.kit.blocks = 5;
    GuardianPlan br = guard.evaluate(with_blocks);
    CHECK(br.intervene && br.action == GuardianAction::BridgeForward && br.place_valid && br.aim_valid);
    CHECK(br.place.x == 8 && br.place.y == 63 && br.place.z == 3);
    CHECK(br.place.support_x == 7 && br.place.support_y == 63 && br.place.face == 5);
    CHECK(br.sneak && br.override_movement && !br.sprint);
    bool halt_listed = false;
    for (const GuardianOption& o : br.options) halt_listed = halt_listed || o.action == GuardianAction::Halt;
    CHECK(halt_listed);  // explainability: every considered option is returned
    // Learned effectiveness can overrule the catalogue gain: if bridging keeps failing, halt wins.
    with_blocks.effectiveness = [](const std::string& id) { return id == "survival.bridge_forward" ? 0.1 : 0.8; };
    CHECK(guard.evaluate(with_blocks).action == GuardianAction::Halt);
    // Garbage in, no action out.
    GuardianInput bad = gin(g, Vec3{std::nan(""), 64.0, 3.5});
    CHECK(!guard.evaluate(bad).intervene);
    GuardianInput nogrid = gin(g, Vec3{7.5, 64.0, 3.5});
    nogrid.grid = nullptr;
    CHECK(!guard.evaluate(nogrid).intervene);
}

void test_guardian_protected_blocks_and_unknown() {
    // The edge block is a BED: it must never be clicked (right-click = sleep) or used as support.
    WorldGrid g(-64, 319);
    g.mark_region_loaded(-48, -48, 96, 96);
    platform(g, 0, 0, 7, 7);
    g.fill(7, 63, 0, 7, 63, 7, BlockClass::Bed);
    SurvivalGuardian guard;
    GuardianInput in = gin(g, Vec3{7.5, 64.0, 3.5});
    in.kit.blocks = 5;
    const GuardianPlan p = guard.evaluate(in);
    CHECK(p.intervene && p.action == GuardianAction::Halt && !p.place_valid);
    for (const GuardianOption& o : p.options)
        if (o.action == GuardianAction::BridgeForward) CHECK(!o.feasible);
    PlacementOrder order;
    CHECK(!find_placement(g, 8, 63, 3, Vec3{7.5, 65.62, 3.5}, 4.0, &order));
    WorldGrid prot(-64, 319);
    prot.mark_region_loaded(-48, -48, 96, 96);
    platform(prot, 0, 0, 7, 7);
    prot.fill(7, 63, 0, 7, 63, 7, BlockClass::Protected);
    CHECK(!find_placement(prot, 8, 63, 3, Vec3{7.5, 65.62, 3.5}, 4.0, &order));
    // Sight line: a wall between the eye and the face blocks the placement.
    WorldGrid walled(-64, 319);
    walled.mark_region_loaded(-48, -48, 96, 96);
    platform(walled, 0, 0, 7, 7);
    walled.fill(7, 64, 0, 7, 65, 7, BlockClass::Solid);  // wall on the very edge
    CHECK(!find_placement(walled, 8, 63, 3, Vec3{5.5, 65.62, 3.5}, 4.0, &order));

    // Unobserved terrain ahead is treated as dangerous (never as air, never as safe).
    WorldGrid unk(-64, 319);
    platform(unk, 0, 0, 7, 7);  // no chunk marked loaded
    const GuardianPlan u = guard.evaluate(gin(unk, Vec3{7.5, 64.0, 3.5}));
    CHECK(u.intervene && u.hazard.kind == HazardKind::UnknownDrop && u.action == GuardianAction::Halt);
}

void test_guardian_airborne_options() {
    // Falling beside a thick pillar: a block can be placed against its side.
    WorldGrid g(-64, 319);
    g.mark_region_loaded(-48, -48, 96, 96);
    g.fill(0, 59, 0, 7, 63, 7, BlockClass::Solid);
    SurvivalGuardian guard;
    GuardianInput in;
    in.grid = &g;
    in.feet = Vec3{8.4, 62.0, 3.5};
    in.on_ground = false;
    in.vertical_velocity = -0.3;
    in.kit.blocks = 3;
    GuardianPlan p = guard.evaluate(in);
    CHECK(p.intervene && p.action == GuardianAction::BlockClutch && p.place_valid);
    CHECK(p.place.support_x == 7 && p.place.x == 8);
    // A thin platform offers no face under the player: block clutch is honestly infeasible.
    WorldGrid thin(-64, 319);
    thin.mark_region_loaded(-48, -48, 96, 96);
    platform(thin, 0, 0, 7, 7);
    GuardianInput in2 = in;
    in2.grid = &thin;
    p = guard.evaluate(in2);
    for (const GuardianOption& o : p.options)
        if (o.action == GuardianAction::BlockClutch) CHECK(!o.feasible);

    // Pearl: refused without calibrated physics, refused when health cannot pay for the landing.
    in2.kit.blocks = 0;
    in2.kit.ender_pearl = true;
    p = guard.evaluate(in2);
    CHECK(p.intervene && p.action == GuardianAction::None && !p.use_pearl);
    in2.kit.pearl_calibrated = true;
    in2.kit.pearl_physics = pearl_physics();
    p = guard.evaluate(in2);
    CHECK(p.action == GuardianAction::PearlEscape && p.use_pearl && p.aim_valid);
    in2.health = 5.0;
    p = guard.evaluate(in2);
    CHECK(!p.use_pearl);
}

void test_guardian_water_window() {
    WorldGrid g(-64, 319);
    g.mark_region_loaded(-48, -48, 96, 96);
    g.fill(-60, 0, -60, 90, 0, 90, BlockClass::Solid);  // distant floor, top face at y=1
    SurvivalGuardian guard;
    GuardianInput in;
    in.grid = &g;
    in.feet = Vec3{8.4, 14.0, 3.5};
    in.on_ground = false;
    in.vertical_velocity = -1.0;
    in.kit.water_bucket = true;
    GuardianPlan p = guard.evaluate(in);
    CHECK(p.intervene && p.action == GuardianAction::WaterClutch && p.aim_valid && !p.use_water);  // armed, not in reach yet
    CHECK(p.aim_pitch_deg > 80.0);
    // At terminal speed one tick skips the entire reach window: the guardian must not count on it.
    GuardianInput fast = in;
    fast.feet = Vec3{8.4, 7.2, 3.5};
    fast.vertical_velocity = -3.8;
    p = guard.evaluate(fast);
    for (const GuardianOption& o : p.options)
        if (o.action == GuardianAction::WaterClutch) CHECK(!o.feasible);
}

// ----------------------------------------------------------------- closed loop

void test_loop_void_edge_halt() {
    Rig r;
    r.level = AssistLevel::SurvivalOnly;
    platform(r.world.grid, 0, 0, 7, 7);
    r.human = [](std::uint64_t) { return run_forward(); };
    r.run(10);
    CHECK(r.last.authority.state == Authority::Human);  // far from the edge the human is not touched
    CHECK(r.last.cmd.sprint && !r.last.cmd.look_override);
    r.run(90);
    CHECK(!r.world.player.dead && r.world.player.pos.x < 8.0 && r.world.player.on_ground);
    CHECK(r.world.rejected.empty() && r.emergency_ticks > 0);
    CHECK(r.techniques.count("survival.sneak_halt") == 1U);
}

void test_loop_void_edge_bridge() {
    Rig r;
    r.level = AssistLevel::SurvivalOnly;
    platform(r.world.grid, 0, 0, 7, 7);
    r.world.player.blocks = 30;
    r.human = [](std::uint64_t) { return run_forward(); };
    r.run(260);
    CHECK(!r.world.player.dead);
    CHECK(r.world.blocks_placed >= 5 && r.world.player.pos.x > 11.0);
    CHECK(r.world.rejected.empty());
    CHECK(r.techniques.count("survival.bridge_forward") == 1U);
}

void test_loop_pearl_rescue() {
    Rig r;
    r.level = AssistLevel::SurvivalOnly;
    platform(r.world.grid, 0, 0, 7, 7);
    r.world.player.pos = Vec3{8.4, 63.5, 3.5};  // knocked just off the east edge
    r.world.player.vy = -0.2;
    r.world.player.on_ground = false;
    r.world.player.pearls = 1;
    r.pearl_calibrated = true;
    r.run(60);
    CHECK(!r.world.player.dead);
    CHECK(r.world.player.pos.y >= 63.9 && r.world.player.pos.x < 7.9);
    CHECK(std::fabs(r.world.player.health - 15.0) < 1e-9);  // the pearl costs 5 health, nothing else
    CHECK(r.techniques.count("survival.pearl_escape") == 1U && r.world.rejected.empty());
    // Without calibrated physics the AI refuses to throw: this fall is not survivable, and it says so.
    Rig u;
    u.level = AssistLevel::SurvivalOnly;
    platform(u.world.grid, 0, 0, 7, 7);
    u.world.player.pos = Vec3{8.4, 63.5, 3.5};
    u.world.player.vy = -0.2;
    u.world.player.on_ground = false;
    u.world.player.pearls = 1;
    u.run(120);
    CHECK(u.world.player.dead && u.world.player.pearls == 1);
}

void test_loop_water_clutch() {
    Rig r;
    r.level = AssistLevel::SurvivalOnly;
    r.world.grid.fill(-60, 0, -60, 90, 0, 90, BlockClass::Solid);
    r.world.player.pos = Vec3{8.4, 30.0, 3.5};
    r.world.player.on_ground = false;
    r.world.player.water_bucket = true;
    r.run(80);
    CHECK(!r.world.player.dead && std::fabs(r.world.player.health - 20.0) < 1e-9);
    CHECK(r.world.grid.get(8, 1, 3) == BlockClass::Water);
    CHECK(r.techniques.count("survival.water_clutch") == 1U && r.world.rejected.empty());
    // Same fall without a bucket is fatal.
    Rig u;
    u.level = AssistLevel::SurvivalOnly;
    u.world.grid.fill(-60, 0, -60, 90, 0, 90, BlockClass::Solid);
    u.world.player.pos = Vec3{8.4, 30.0, 3.5};
    u.world.player.on_ground = false;
    u.run(80);
    CHECK(u.world.player.dead && u.world.player.death_cause == "fall");
}

void test_loop_fight_gating_and_takeover() {
    // Activation by human attack input: AI controls aim/positioning, the human's hold enables swings.
    Rig r;
    platform(r.world.grid, 0, 0, 15, 15);
    r.world.player.pos = Vec3{3.5, 64.0, 3.5};
    r.world.player.yaw = 0.0;
    SimEnemy e;
    e.id = "A";
    e.style = EnemyStyle::Camper;
    e.pos = Vec3{3.5, 64.0, 10.5};
    e.damage = 0.5;
    r.world.enemies.push_back(e);
    r.human = [](std::uint64_t) {
        HumanIntentFrame f;
        f.attack_held = true;
        return f;
    };
    r.run(120);
    CHECK(r.world.damage_dealt >= 6.0);
    bool drove = false;
    for (Authority a : r.states) drove = drove || a == Authority::AiDriving;
    CHECK(drove);
    CHECK(!r.hub.current_technique().empty() || r.world.enemies[0].health <= 0.0);
    // Once the enemy is dead the AI has nothing to drive and hands the character back.
    if (r.world.enemies[0].health <= 0.0) CHECK(r.states.back() == Authority::Assist);

    // No attack input: the AI may position and aim but never swings.
    Rig g;
    platform(g.world.grid, 0, 0, 15, 15);
    g.world.player.yaw = 0.0;
    g.world.enemies.push_back(e);
    g.run(120);
    CHECK(g.world.damage_dealt == 0.0);
    bool denied = false;
    for (int i = 0; i < 40 && !denied; ++i) {
        g.step();
        for (const std::string& d : g.last.denials) denied = denied || d.find("attack needs the human") != std::string::npos;
    }
    CHECK(denied);

    // No measured attack reach: the AI never attacks, even with the human holding attack.
    HubConfig no_reach;
    Rig n(no_reach);
    platform(n.world.grid, 0, 0, 15, 15);
    n.world.player.yaw = 0.0;
    n.world.enemies.push_back(e);
    n.human = r.human;
    // (the human's own swings still land; the AI contributes none)
    bool ai_attacked = false;
    for (int i = 0; i < 120; ++i) {
        n.step();
        ai_attacked = ai_attacked || n.last.cmd.attack;
    }
    CHECK(!ai_attacked);
}

void test_loop_second_input_takes_control() {
    Rig r;
    platform(r.world.grid, 0, 0, 15, 15);
    r.world.player.yaw = 0.0;
    SimEnemy e;
    e.id = "A";
    e.style = EnemyStyle::Camper;
    e.pos = Vec3{3.5, 64.0, 13.5};
    e.damage = 0.0;
    r.world.enemies.push_back(e);
    r.human = [](std::uint64_t t) {
        HumanIntentFrame f;
        if (t == 20 || t == 28) f.look_dx = 40.0;  // two separate turns
        return f;
    };
    r.run(20);
    CHECK(r.states.back() == Authority::AiDriving);
    const double yaw_before = r.world.player.yaw;
    r.step();  // tick 20: first turn
    CHECK(r.last.authority.state == Authority::HumanContesting && r.last.authority.ai_looks);
    CHECK(std::fabs(r.world.player.yaw - yaw_before) < 90.0 && r.last.cmd.look_override);  // AI kept the camera
    r.run(7);
    CHECK(r.last.authority.state == Authority::HumanContesting);
    r.step();  // tick 28: second turn
    CHECK(r.last.authority.human_took_control && r.last.authority.state == Authority::Assist);
    CHECK(!r.last.cmd.look_override);  // camera is the human's again
    // The running technique is penalised: the human changed the plan.
    CHECK(r.hub.current_technique().empty());
    r.run(30);
    CHECK(r.states.back() == Authority::Assist);  // held off while the human is in charge
}

void test_loop_human_runs_to_void_after_takeover() {
    Rig r;
    platform(r.world.grid, 0, 0, 15, 15);
    r.world.player.yaw = 0.0;
    SimEnemy e;
    e.id = "A";
    e.style = EnemyStyle::Camper;
    e.pos = Vec3{3.5, 64.0, 13.5};
    e.damage = 0.0;
    r.world.enemies.push_back(e);
    r.human = [](std::uint64_t t) {
        HumanIntentFrame f;
        if (t == 12 || t == 18) f.look_dx = 20.0;       // change of plan: two turns
        if (t == 19) f.look_dx = -90.0 - 40.0;          // face +X
        if (t >= 20 && t < 120) {                        // ... and run for the void edge
            f.move_y = 1.0;
            f.sprint_held = true;
        }
        return f;
    };
    r.run(18);
    CHECK(r.human_took || r.states.back() != Authority::AiDriving);
    r.run(120);
    CHECK(!r.world.player.dead);
    CHECK(r.world.player.pos.x < 16.0 && r.emergency_ticks > 0);
    CHECK(r.techniques.count("survival.sneak_halt") == 1U);
    r.human = [](std::uint64_t) { return HumanIntentFrame{}; };
    r.run(30);
    CHECK(r.states.back() == Authority::Assist || r.states.back() == Authority::AiDriving);
    CHECK(r.world.rejected.empty());
}

void test_loop_explicit_override_is_p0() {
    Rig r;
    platform(r.world.grid, 0, 0, 15, 15);
    r.human = [](std::uint64_t) {
        HumanIntentFrame f = run_forward();
        f.explicit_override = true;  // the dedicated override button is held
        return f;
    };
    r.run(150);
    CHECK(r.world.player.dead && r.world.player.death_cause == "void");
    CHECK(r.last.authority.state == Authority::Human && r.emergency_ticks == 0);
}

void test_loop_bridge_by_intent() {
    Rig r;
    r.level = AssistLevel::Assist;
    platform(r.world.grid, 0, 0, 7, 7);
    platform(r.world.grid, 13, 0, 40, 7);  // gap of 5 blocks
    r.world.player.pos = Vec3{6.5, 64.0, 3.5};
    r.world.player.blocks = 20;
    r.human = [](std::uint64_t) {
        HumanIntentFrame f;
        f.move_y = 1.0;
        f.place_held = true;
        return f;
    };
    r.run(70);
    CHECK(!r.world.player.dead && r.world.player.pos.x > 13.5);
    CHECK(r.world.blocks_placed == 5);  // exactly the five missing cells, nothing more
    CHECK(r.world.rejected.empty());
    CHECK(r.techniques.count("bridge.sneak_straight") == 1U);

    // Normal placement needs the human's place input.  Isolate that rule by switching the survival
    // override off (a user choice): with no place input the AI never places, and the human is on their own.
    HubConfig no_guard = Rig::config();
    no_guard.takeover.survival_overrides_human = false;
    Rig n(no_guard);
    n.level = AssistLevel::Assist;
    platform(n.world.grid, 0, 0, 7, 7);
    platform(n.world.grid, 13, 0, 40, 7);
    n.world.player.pos = Vec3{6.5, 64.0, 3.5};
    n.world.player.blocks = 20;
    n.human = [](std::uint64_t) {
        HumanIntentFrame f;
        f.move_y = 1.0;
        return f;
    };
    n.run(200);
    CHECK(n.world.blocks_placed == 0 && n.world.player.dead && n.emergency_ticks == 0);
    // Place input released mid-way: placement stops at once.
    Rig m(no_guard);
    m.level = AssistLevel::Assist;
    platform(m.world.grid, 0, 0, 7, 7);
    platform(m.world.grid, 13, 0, 40, 7);
    m.world.player.pos = Vec3{6.5, 64.0, 3.5};
    m.world.player.blocks = 20;
    m.human = [](std::uint64_t t) {
        HumanIntentFrame f;
        if (t < 24) {
            f.move_y = 1.0;
            f.place_held = true;
        }
        return f;
    };
    m.run(24);
    const int placed_while_held = m.world.blocks_placed;
    CHECK(placed_while_held >= 2);
    m.run(120);
    CHECK(!m.world.player.dead && m.world.blocks_placed == placed_while_held);
    // With the survival override ON (default), an unarmed sprint at the edge is rescued; the AI may
    // use its emergency exception to place blocks even without place input.
    Rig e;
    e.level = AssistLevel::Assist;
    platform(e.world.grid, 0, 0, 7, 7);
    e.world.player.blocks = 20;
    e.human = [](std::uint64_t) { return run_forward(); };
    e.run(150);
    CHECK(!e.world.player.dead && e.emergency_ticks > 0 && e.world.rejected.empty());
}

void test_loop_diagonal_bridge() {
    Rig r;
    r.level = AssistLevel::Assist;
    platform(r.world.grid, 0, 0, 7, 7);
    platform(r.world.grid, 12, 8, 40, 20);  // diagonal gap
    r.world.player.pos = Vec3{6.5, 64.0, 6.5};
    r.world.player.yaw = -45.0;  // forward = (+X, +Z)
    r.world.player.blocks = 40;
    r.human = [](std::uint64_t) {
        HumanIntentFrame f;
        f.move_y = 1.0;
        f.place_held = true;
        return f;
    };
    r.run(220);
    CHECK(!r.world.player.dead);
    CHECK(r.techniques.count("bridge.sneak_diagonal") == 1U);
    CHECK(r.world.blocks_placed >= 6 && r.world.rejected.empty());
    CHECK(r.world.player.pos.x > 11.5 && r.world.player.pos.z > 7.5);  // crossed to the far platform
}

void test_policy_gate() {
    Rig r;
    r.cls = ServerClass::PublicCompetitive;  // default ceiling: observe only
    r.level = AssistLevel::Takeover;
    platform(r.world.grid, 0, 0, 7, 7);
    r.world.player.pos = Vec3{7.5, 64.0, 3.5};
    r.human = [](std::uint64_t) { return run_forward(); };
    r.step();
    CHECK(r.last.effective_level == AssistLevel::ObserveOnly);
    CHECK(r.last.authority.state == Authority::Human && !r.last.authority.ai_moves && !r.last.authority.emergency);
    CHECK(r.last.cmd.sprint && !r.last.cmd.look_override && !r.last.cmd.place && !r.last.cmd.attack);
    CHECK(!r.last.denials.empty());
    // A grant that records the user's confirmation lifts the ceiling for that one server.
    std::string err;
    CHECK(!r.hub.policy().grant(AutomationGrant{"sim|test", AssistLevel::Takeover, false}, &err));
    CHECK(r.hub.policy().grant(AutomationGrant{"sim|test", AssistLevel::SurvivalOnly, true}, &err));
    r.level = AssistLevel::Takeover;
    r.step();
    CHECK(r.last.effective_level == AssistLevel::SurvivalOnly);
    CHECK(r.last.authority.emergency && r.last.cmd.sneak);
    CHECK(!r.hub.policy().automation_permitted("another|server", ServerClass::PublicSocial));
    CHECK(r.hub.policy().automation_permitted("mine", ServerClass::OwnWorld));
}

void test_property_never_dies_to_void_with_guard() {
    // Random platforms, random human scripts (including jumping and strafing): with the guardian
    // on and nothing in the kit, the human can never walk, sprint or jump off a known edge.
    std::uint64_t s = 987654321;
    auto rnd = [&s]() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(s >> 11U) / 9007199254740992.0;
    };
    int runs = 0;
    for (int n = 0; n < 60; ++n) {
        Rig r;
        r.level = AssistLevel::SurvivalOnly;
        const int w = 6 + static_cast<int>(rnd() * 8.0);
        const int d = 6 + static_cast<int>(rnd() * 8.0);
        platform(r.world.grid, 0, 0, w, d);
        r.world.player.pos = Vec3{0.5 + rnd() * w, 64.0, 0.5 + rnd() * d};
        r.world.player.yaw = std::floor(rnd() * 8.0) * 45.0;
        double fwd = 1.0, str = 0.0;
        bool sprint = true, jump = false;
        r.human = [&](std::uint64_t t) {
            if (t % 15 == 0) {
                fwd = std::floor(rnd() * 3.0) - 1.0;
                str = std::floor(rnd() * 3.0) - 1.0;
                sprint = rnd() < 0.7;
                jump = rnd() < 0.3;
            }
            HumanIntentFrame f;
            f.move_y = fwd;
            f.move_x = str;
            f.sprint_held = sprint;
            f.jump_held = jump;
            f.look_dx = (t % 40 == 0) ? (rnd() * 120.0 - 60.0) : 0.0;
            return f;
        };
        r.run(300);
        CHECK(!r.world.player.dead);
        ++runs;
    }
    CHECK(runs == 60);
}

} // namespace

int main() {
    test_guardian_lead_and_halt();
    test_guardian_protected_blocks_and_unknown();
    test_guardian_airborne_options();
    test_guardian_water_window();
    test_loop_void_edge_halt();
    test_loop_void_edge_bridge();
    test_loop_pearl_rescue();
    test_loop_water_clutch();
    test_loop_fight_gating_and_takeover();
    test_loop_second_input_takes_control();
    test_loop_human_runs_to_void_after_takeover();
    test_loop_explicit_override_is_p0();
    test_loop_bridge_by_intent();
    test_loop_diagonal_bridge();
    test_policy_gate();
    test_property_never_dies_to_void_with_guard();
    std::cout << "v37 tests passed\n";
    return 0;
}
