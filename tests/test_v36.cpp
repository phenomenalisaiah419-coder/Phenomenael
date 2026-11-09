#include "core/hazard_scan.hpp"
#include "core/item_learner.hpp"
#include "core/opponent_model.hpp"
#include "core/world_grid.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace bedrockai;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAIL: " << #condition << " at line " << __LINE__ << "\n"; \
    std::exit(1); } } while (false)

namespace {

constexpr double kFaceX = -90.0;  // yaw whose forward vector is +X

// 8x8 platform: solid cells y=63, x,z in [0,7]; feet stand at y=64.  Everything in the region is
// loaded air otherwise, so beyond the edge is a KNOWN void.
WorldGrid platform_world(bool loaded = true) {
    WorldGrid g(-64, 319);
    if (loaded) g.mark_region_loaded(-32, -32, 64, 64);
    g.fill(0, 63, 0, 7, 63, 7, BlockClass::Solid);
    return g;
}

HazardReport run_scan(const WorldGrid& g, Vec3 feet, double yaw, bool sprint, bool sneak, bool jump, int ticks = 30,
                      double health = 20.0) {
    MotionPredictor mp;
    HazardScanner hs;
    const auto path = mp.predict(g, feet, 0.0, true, yaw, 1.0, 0.0, sprint, sneak, jump, ticks);
    return hs.scan_path(g, path, health);
}

void test_grid() {
    // Unknown is not air; loaded chunks are air; explicit blocks win.
    WorldGrid g;
    CHECK(g.get(5, 70, 5) == BlockClass::Unknown);
    g.mark_chunk_loaded(5, 5);
    CHECK(g.get(5, 70, 5) == BlockClass::Air);
    CHECK(g.get(15, 70, 15) == BlockClass::Air);   // same chunk
    CHECK(g.get(16, 70, 5) == BlockClass::Unknown); // next chunk
    g.mark_chunk_loaded(-1, -1);
    CHECK(g.get(-16, 0, -16) == BlockClass::Air && g.get(-17, 0, -1) == BlockClass::Unknown);
    g.set(5, 70, 5, BlockClass::Solid);
    CHECK(g.get(5, 70, 5) == BlockClass::Solid);
    g.erase(5, 70, 5);
    CHECK(g.get(5, 70, 5) == BlockClass::Air);

    // Key packing is collision-free where the legacy block_key() collides.
    CHECK(block_key(1, 0, 0) == block_key(0, 0, 4194304));  // audit finding: legacy hash collides
    CHECK(WorldGrid::pack(1, 0, 0) != WorldGrid::pack(0, 0, 4194304));
    std::set<std::uint64_t> keys;
    const int xs[] = {-30000000, -4194304, -1, 0, 1, 4194304, 30000000};
    const int ys[] = {-64, -1, 0, 64, 319};
    for (int x : xs)
        for (int y : ys)
            for (int z : xs) keys.insert(WorldGrid::pack(x, y, z));
    CHECK(keys.size() == 7U * 5U * 7U);

    // Decoder bridge: unknown stays unknown unless a classifier says otherwise.
    WorldState ws;
    BlockObservation b1;
    b1.x = 1; b1.y = 2; b1.z = 3; b1.solid = true;
    BlockObservation b2;
    b2.x = 4; b2.y = 5; b2.z = 6; b2.solid = false; b2.runtime_id = 77;
    ws.blocks.emplace(block_key(b1.x, b1.y, b1.z), b1);
    ws.blocks.emplace(block_key(b2.x, b2.y, b2.z), b2);
    const WorldGrid d = WorldGrid::from_world_state(ws);
    CHECK(d.get(1, 2, 3) == BlockClass::Solid && d.get(4, 5, 6) == BlockClass::Unknown);
    const WorldGrid c = WorldGrid::from_world_state(ws, [](const BlockObservation& o) {
        return o.runtime_id == 77 ? BlockClass::Lava : (o.solid ? BlockClass::Solid : BlockClass::Unknown);
    });
    CHECK(c.get(4, 5, 6) == BlockClass::Lava);
}

void test_void_edge_prediction() {
    const WorldGrid g = platform_world();
    // Human sprints toward the +X edge from the middle of the platform.
    HazardReport r = run_scan(g, Vec3{3.5, 64.0, 3.5}, kFaceX, true, false, false);
    CHECK(r.found && r.kind == HazardKind::Void && r.lethal && r.certain);
    CHECK(r.tick >= 14 && r.tick <= 20);  // ~ (8.3-3.5)/0.2806 ticks until fully unsupported
    // Walking is slower: the hazard is later.
    HazardReport w = run_scan(g, Vec3{3.5, 64.0, 3.5}, kFaceX, false, false, false, 40);
    CHECK(w.found && w.tick > r.tick);
    // Sneaking never leaves the platform (edge protection): no hazard.
    HazardReport s = run_scan(g, Vec3{3.5, 64.0, 3.5}, kFaceX, false, true, false, 120);
    CHECK(!s.found);
    // Away from the edge (facing -X) nothing is dangerous within the horizon.
    CHECK(!run_scan(g, Vec3{3.5, 64.0, 3.5}, 90.0, true, false, false, 12).found);
    // A wall stops the predicted path: no fall.
    WorldGrid walled = platform_world();
    walled.fill(6, 64, 0, 6, 65, 7, BlockClass::Solid);
    CHECK(!run_scan(walled, Vec3{3.5, 64.0, 3.5}, kFaceX, true, false, false, 60).found);
}

void test_fall_outcomes() {
    // Ground 34 blocks below: lethal fall.
    WorldGrid deep = platform_world();
    deep.fill(-32, 29, -32, 64, 29, 64, BlockClass::Solid);
    HazardReport r = run_scan(deep, Vec3{7.5, 64.0, 3.5}, kFaceX, true, false, false, 60);
    CHECK(r.found && r.kind == HazardKind::LethalFall && r.lethal && r.expected_damage >= 20.0);
    // Same fall with 3 blocks of drop: no damage, so no hazard.
    WorldGrid shallow = platform_world();
    shallow.fill(-32, 60, -32, 64, 60, 64, BlockClass::Solid);
    shallow.fill(0, 60, 0, 7, 60, 7, BlockClass::Solid);
    CHECK(!run_scan(shallow, Vec3{7.5, 64.0, 3.5}, kFaceX, true, false, false, 60).found);
    // 6 blocks: ceil(6-3) = 3 damage, survivable.
    WorldGrid mid = platform_world();
    mid.fill(-32, 57, -32, 64, 57, 64, BlockClass::Solid);
    r = run_scan(mid, Vec3{7.5, 64.0, 3.5}, kFaceX, true, false, false, 60);
    CHECK(r.found && r.kind == HazardKind::DamagingFall && !r.lethal && std::fabs(r.expected_damage - 3.0) < 1e-9);
    // Low health makes the same fall lethal.
    r = run_scan(mid, Vec3{7.5, 64.0, 3.5}, kFaceX, true, false, false, 60, 3.0);
    CHECK(r.found && r.kind == HazardKind::LethalFall && r.lethal);
    // Water cushions a 23-block fall; lava is lethal; slime cancels it.
    WorldGrid water = platform_world();
    water.fill(-32, 40, -32, 64, 40, 64, BlockClass::Water);
    CHECK(!run_scan(water, Vec3{7.5, 64.0, 3.5}, kFaceX, true, false, false, 60).found);
    WorldGrid lava = platform_world();
    lava.fill(-32, 40, -32, 64, 40, 64, BlockClass::Lava);
    r = run_scan(lava, Vec3{7.5, 64.0, 3.5}, kFaceX, true, false, false, 60);
    CHECK(r.found && r.kind == HazardKind::Lava && r.lethal);
    WorldGrid slime = platform_world();
    slime.fill(-32, 30, -32, 64, 30, 64, BlockClass::Bounce);
    CHECK(!run_scan(slime, Vec3{7.5, 64.0, 3.5}, kFaceX, true, false, false, 80).found);
}

void test_unknown_and_walking_into_hazards() {
    // Chunks never observed: the drop is UNKNOWN, reported as such and never as safe or as void.
    const WorldGrid g = platform_world(false);
    HazardReport r = run_scan(g, Vec3{7.5, 64.0, 3.5}, kFaceX, true, false, false, 30);
    CHECK(r.found && r.kind == HazardKind::UnknownDrop && !r.certain && !r.lethal);
    // Walking into lava on the ground.
    WorldGrid lava = platform_world();
    lava.fill(5, 64, 0, 5, 64, 7, BlockClass::Lava);
    r = run_scan(lava, Vec3{3.5, 64.0, 3.5}, kFaceX, false, false, false, 20);
    CHECK(r.found && r.kind == HazardKind::Lava && r.lethal);
    // Standing on a harmful block is flagged but not lethal.
    WorldGrid cactus = platform_world();
    cactus.fill(5, 63, 0, 5, 63, 7, BlockClass::Hazard);
    r = run_scan(cactus, Vec3{3.5, 64.0, 3.5}, kFaceX, false, false, false, 20);
    CHECK(r.found && r.kind == HazardKind::HarmfulBlock && !r.lethal);
}

void test_gap_jump() {
    // Two platforms with a gap along X: a sprint-jump clears 2 blocks but not 5.
    auto build = [](int gap) {
        WorldGrid g(-64, 319);
        g.mark_region_loaded(-32, -32, 64, 64);
        g.fill(0, 63, 0, 7, 63, 7, BlockClass::Solid);
        g.fill(8 + gap, 63, 0, 60, 63, 7, BlockClass::Solid);
        return g;
    };
    // Jump from the very edge so the human controls the timing.
    CHECK(!run_scan(build(2), Vec3{7.6, 64.0, 3.5}, kFaceX, true, false, true, 40).found);
    HazardReport far_gap = run_scan(build(5), Vec3{7.6, 64.0, 3.5}, kFaceX, true, false, true, 60);
    CHECK(far_gap.found && far_gap.kind == HazardKind::Void);
}

// ----------------------------------------------------------------- opponents

struct Strafer {
    double x = 0.0;
    double z = 4.0;
    double vx = 0.18;
    int period = 10;
    std::uint64_t t = 0;
    void step() {
        ++t;
        if (t % static_cast<std::uint64_t>(period) == 0U) vx = -vx;
        x += vx;
    }
};

void test_opponent_model() {
    const Vec3 me{0.0, 64.0, 0.0};
    // Strafer: classified, rhythm recovered, and the learned prediction beats linear extrapolation.
    {
        std::vector<Vec3> truth;
        Strafer s;
        for (int i = 0; i < 200; ++i) {
            truth.push_back(Vec3{s.x, 64.0, s.z});
            s.step();
        }
        OpponentModel m;
        double err_learned = 0.0, err_linear = 0.0;
        int n = 0;
        for (int t = 0; t < 120; ++t) {
            m.observe("S", static_cast<std::uint64_t>(t + 1), truth[static_cast<std::size_t>(t)], me);
            if (t < 50) continue;
            const Vec3 truth_future = truth[static_cast<std::size_t>(t + 8)];
            const Vec3 pl = m.predict_position("S", 8);
            const Vec3 pn = m.predict_linear("S", 8);
            err_learned += std::hypot(pl.x - truth_future.x, pl.z - truth_future.z);
            err_linear += std::hypot(pn.x - truth_future.x, pn.z - truth_future.z);
            ++n;
        }
        const OpponentProfile p = m.profile("S");
        CHECK(p.archetype == Archetype::Strafer && p.confidence > 0.6);
        CHECK(std::fabs(p.strafe_period_ticks - 10.0) < 1.6);
        CHECK(err_learned / n < 0.55 * (err_linear / n));
    }
    // Rusher, evasive, defensive, ranged, and "not enough data".
    {
        OpponentModel m;
        for (int i = 0; i < 30; ++i) {
            const auto t = static_cast<std::uint64_t>(i + 1);
            m.observe("R", t, Vec3{0.0, 64.0, 20.0 - 0.25 * i}, me);
            m.observe("E", t, Vec3{0.0, 64.0, 6.0 + 0.2 * i}, me);
            m.observe("D", t, Vec3{3.0, 64.0, 3.0}, me);
            m.observe("G", t, Vec3{0.0, 64.0, 12.0}, me, i % 2 == 0);
        }
        m.observe("N", 1, Vec3{1.0, 64.0, 1.0}, me);
        CHECK(m.profile("R").archetype == Archetype::Rusher);
        CHECK(m.profile("E").archetype == Archetype::Evasive);
        CHECK(m.profile("D").archetype == Archetype::Defensive);
        CHECK(m.profile("G").archetype == Archetype::Ranged);
        CHECK(m.profile("N").archetype == Archetype::Unknown && m.profile("N").confidence < 0.5);
        // A rusher is predicted to keep closing the distance.
        CHECK(m.predict_position("R", 4).z < 20.0 - 0.25 * 29 - 0.5);
        CHECK(m.profile("missing").archetype == Archetype::Unknown);
    }
    // Hostile input and bounded memory.
    {
        OpponentModelConfig cfg;
        cfg.max_opponents = 4;
        OpponentModel m(cfg);
        m.observe("bad", 1, Vec3{std::nan(""), 0.0, 0.0}, me);
        CHECK(m.tracked() == 0U);
        for (int i = 0; i < 100; ++i) m.observe("o" + std::to_string(i), static_cast<std::uint64_t>(i + 1), Vec3{1.0, 64.0, 1.0}, me);
        CHECK(m.tracked() <= 4U);
        m.observe("o99", 50, Vec3{9.0, 64.0, 9.0}, me);  // stale tick ignored
        CHECK(m.profile("o99").samples == 1);
    }
}

// --------------------------------------------------------------------- items

ItemTrial blast_use(double up) {
    ItemTrial t;
    t.item_key = "srv:blast_staff|v1";
    t.activation = ItemActivationMode::Use;
    t.by_human = true;
    t.self_velocity_delta = Vec3{0.0, up, 0.0};
    return t;
}

std::vector<TimedPosition> flight(double speed, double angle_deg, double gravity, double drag, int n) {
    std::vector<TimedPosition> out;
    const double a = angle_deg * 3.14159265358979323846 / 180.0;
    double vx = speed * std::cos(a);
    double vy = speed * std::sin(a);
    double x = 0.0, y = 64.0;
    for (int i = 0; i < n; ++i) {
        out.push_back(TimedPosition{static_cast<std::uint64_t>(100 + i), Vec3{x, y, 0.0}});
        x += vx;
        y += vy;
        vx *= drag;
        vy = vy * drag - gravity;
    }
    return out;
}

void test_item_learning() {
    ItemLearner L;
    // Not learned from a name, nor from one or two uses.
    L.observe(blast_use(0.9));
    L.observe(blast_use(0.9));
    CHECK(L.learned("srv:blast_staff|v1").capabilities == 0U);
    CHECK(!L.usable_for("srv:blast_staff|v1", ItemCapability::ClutchRecovery));
    L.observe(blast_use(0.9));
    const LearnedItem li = L.learned("srv:blast_staff|v1");
    CHECK(has_capability(li.capabilities, ItemCapability::BurstMovement));
    CHECK(has_capability(li.capabilities, ItemCapability::ClutchRecovery));
    CHECK(li.trials == 3U && li.confidence >= 0.75);
    CHECK(L.usable_for("srv:blast_staff|v1", ItemCapability::ClutchRecovery));
    CHECK(!L.usable_for("srv:blast_staff|v1", ItemCapability::Healing));
    CHECK(!L.usable_for("never:seen", ItemCapability::Healing));

    // Inconsistent evidence is not enough (2 of 5 trials is below the confidence bar).
    ItemLearner flaky;
    for (int i = 0; i < 5; ++i) {
        ItemTrial t = blast_use(i < 2 ? 0.9 : 0.0);
        t.item_key = "srv:flaky";
        flaky.observe(t);
    }
    CHECK(!flaky.usable_for("srv:flaky", ItemCapability::BurstMovement));

    // Harmful consumable: one bad trial flags it unsafe for good.
    ItemLearner bad;
    ItemTrial hurt;
    hurt.item_key = "srv:weird_potion";
    hurt.activation = ItemActivationMode::Consume;
    hurt.self_health_delta = -4.0;
    bad.observe(hurt);
    for (int i = 0; i < 4; ++i) {
        ItemTrial heal = hurt;
        heal.self_health_delta = 6.0;
        bad.observe(heal);
    }
    CHECK(bad.learned("srv:weird_potion").unsafe);
    CHECK(!bad.usable_for("srv:weird_potion", ItemCapability::Healing));
    ItemKnowledgeBase kb;
    CHECK(!bad.promote("srv:weird_potion", ItemDescriptor{"srv:weird_potion", "Weird Potion", {}, ""}, kb));

    // Healing item learned from observation.
    ItemLearner heal_l;
    for (int i = 0; i < 3; ++i) {
        ItemTrial t;
        t.item_key = "srv:med_gel";
        t.activation = ItemActivationMode::Consume;
        t.self_health_delta = 5.0;
        t.by_human = true;
        heal_l.observe(t);
    }
    CHECK(heal_l.usable_for("srv:med_gel", ItemCapability::Healing));
    CHECK(has_capability(heal_l.learned("srv:med_gel").capabilities, ItemCapability::Drinkable));

    // Projectile physics are fitted from flight samples and calibrated only after consistent fits.
    ItemLearner proj;
    const double angles[] = {20.0, 32.0, 44.0};
    for (int i = 0; i < 2; ++i) {
        ItemTrial t;
        t.item_key = "srv:pearl_like";
        t.activation = ItemActivationMode::Throw;
        t.projectile_spawned = true;
        t.projectile_samples = flight(1.5, angles[i], 0.03, 0.99, 8);
        proj.observe(t);
    }
    CHECK(!proj.usable_for("srv:pearl_like", ItemCapability::Projectile));  // 2 trials: below min_trials
    ItemTrial t3;
    t3.item_key = "srv:pearl_like";
    t3.activation = ItemActivationMode::Throw;
    t3.projectile_spawned = true;
    t3.projectile_samples = flight(1.5, angles[2], 0.03, 0.99, 8);
    proj.observe(t3);
    const LearnedItem pl = proj.learned("srv:pearl_like");
    CHECK(pl.projectile.has_value() && pl.projectile->calibrated);
    CHECK(std::fabs(pl.projectile->launch_speed - 30.0) < 0.5);
    CHECK(std::fabs(pl.projectile->gravity - 12.0) < 0.2);
    CHECK(std::fabs(pl.projectile->drag_per_tick - 0.99) < 0.002);
    CHECK(proj.usable_for("srv:pearl_like", ItemCapability::Throwable));

    // Inconsistent flights never calibrate.
    ItemLearner noisy;
    const double speeds[] = {1.0, 1.9, 1.4};
    for (int i = 0; i < 3; ++i) {
        ItemTrial t;
        t.item_key = "srv:odd";
        t.activation = ItemActivationMode::Throw;
        t.projectile_spawned = true;
        t.projectile_samples = flight(speeds[i], 30.0, 0.03, 0.99, 8);
        noisy.observe(t);
    }
    CHECK(!noisy.usable_for("srv:odd", ItemCapability::Projectile));

    // Promotion into the existing knowledge base makes it a verified profile.
    ItemKnowledgeBase base;
    std::string err;
    CHECK(L.promote("srv:blast_staff|v1", ItemDescriptor{"srv:blast_staff", "Blast Staff", {}, "v1"}, base, &err));
    const ItemKnowledge k = base.resolve(ItemDescriptor{"srv:blast_staff", "Blast Staff", {}, "v1"});
    CHECK(k.verified && has_capability(k.capabilities, ItemCapability::BurstMovement));
    CHECK(!L.promote("srv:unknown", ItemDescriptor{"srv:unknown", "", {}, ""}, base, &err));

    // Hostile trials are ignored; memory is bounded.
    ItemLearner lim(ItemLearnerConfig{3, 0.75, 0.4, 1.0, 2.0, 1.0, 0.3, 3.0, 6, 5, 20.0});
    ItemTrial nan_trial = blast_use(0.9);
    nan_trial.self_health_delta = std::nan("");
    lim.observe(nan_trial);
    CHECK(lim.tracked() == 0U);
    for (int i = 0; i < 20; ++i) {
        ItemTrial t = blast_use(0.9);
        t.item_key = "item" + std::to_string(i);
        lim.observe(t);
    }
    CHECK(lim.tracked() <= 5U);
}

void test_experiment_gate() {
    ItemLearner L;
    ExperimentContext ok;
    ok.user_allows_experiments = true;
    ok.automation_permitted = true;
    ok.footing_safe = true;
    ok.health_fraction = 1.0;
    CHECK(L.experiment_verdict("srv:x", ok).allowed);
    auto denied = [&](auto mutate) {
        ExperimentContext c = ok;
        mutate(c);
        return !L.experiment_verdict("srv:x", c).allowed;
    };
    CHECK(denied([](ExperimentContext& c) { c.user_allows_experiments = false; }));
    CHECK(denied([](ExperimentContext& c) { c.automation_permitted = false; }));
    CHECK(denied([](ExperimentContext& c) { c.enemies_nearby = true; }));
    CHECK(denied([](ExperimentContext& c) { c.emergency = true; }));
    CHECK(denied([](ExperimentContext& c) { c.footing_safe = false; }));
    CHECK(denied([](ExperimentContext& c) { c.health_fraction = 0.5; }));
    CHECK(denied([](ExperimentContext& c) { c.activation = ItemActivationMode::Consume; }));
    CHECK(denied([](ExperimentContext& c) { c.activation = ItemActivationMode::Attack; }));
    ExperimentContext consume = ok;
    consume.activation = ItemActivationMode::Consume;
    consume.user_allows_consumables = true;
    CHECK(L.experiment_verdict("srv:x", consume).allowed);
    // Trial budget: after 6 AI trials no more.
    for (int i = 0; i < 6; ++i) {
        ItemTrial t = blast_use(0.0);
        t.item_key = "srv:x";
        t.by_human = false;
        L.observe(t);
    }
    CHECK(!L.experiment_verdict("srv:x", ok).allowed);
}

} // namespace

int main() {
    test_grid();
    test_void_edge_prediction();
    test_fall_outcomes();
    test_unknown_and_walking_into_hazards();
    test_gap_jump();
    test_opponent_model();
    test_item_learning();
    test_experiment_gate();
    std::cout << "v36 tests passed\n";
    return 0;
}
