#include "core/intent_predictor.hpp"
#include "core/skill_learner.hpp"
#include "core/takeover_authority.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace bedrockai;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAIL: " << #condition << " at line " << __LINE__ << "\n"; \
    std::exit(1); } } while (false)

namespace {

HumanIntentFrame stick(double x, double y) {
    HumanIntentFrame f;
    f.move_x = x;
    f.move_y = y;
    return f;
}
HumanIntentFrame flick(double dx) {
    HumanIntentFrame f;
    f.look_dx = dx;
    return f;
}

struct Rig {
    HumanIntentPredictor predictor;
    TakeoverAuthority authority;
    std::uint64_t tick = 0;
    double yaw = 0.0;
    bool plan = true;
    std::optional<double> ai_heading = 0.0;  // AI is moving along +Z
    bool emergency = false;

    explicit Rig(UserMode mode, TakeoverConfig cfg = {}) : authority(cfg) { authority.set_user_mode(mode); }

    AuthorityDecision step(const HumanIntentFrame& f) {
        predictor.observe(tick, f, yaw);
        AuthorityInput in;
        in.tick = tick;
        in.human = f;
        in.predicted = predictor.current();
        in.player_yaw_deg = yaw;
        in.ai_has_plan = plan;
        in.ai_heading_deg = ai_heading;
        in.emergency_request = emergency;
        ++tick;
        return authority.update(in);
    }
    AuthorityDecision idle(int n) {
        AuthorityDecision d;
        for (int i = 0; i < n; ++i) d = step(HumanIntentFrame{});
        return d;
    }
};

void test_predictor() {
    // Heading convention matches MovementController: yaw 0 faces +Z, forward=(-sin,cos).
    CHECK(std::fabs(HumanIntentPredictor::stick_heading_deg(0.0, 1.0, 0.0)) < 1e-9);
    CHECK(std::fabs(HumanIntentPredictor::stick_heading_deg(0.0, 1.0, 90.0) - 90.0) < 1e-9);
    CHECK(std::fabs(HumanIntentPredictor::stick_heading_deg(0.0, -1.0, 0.0) - 180.0) < 1e-9);
    CHECK(std::fabs(HumanIntentPredictor::stick_heading_deg(1.0, 0.0, 0.0) + 90.0) < 1e-9);
    CHECK(HumanIntentPredictor::angle_diff_deg(179.0, -179.0) < 2.001);

    HumanIntentPredictor p;
    for (int i = 0; i < 20; ++i) p.observe(static_cast<std::uint64_t>(i), stick(0.0, 1.0), 0.0);
    CHECK(p.current().moving && p.current().kind == IntentKind::Move);
    CHECK(p.current().keep_heading_probability > 0.9);  // steady runner keeps heading
    CHECK(p.current().confidence > 0.8);

    HumanIntentPredictor jitter;
    for (int i = 0; i < 20; ++i)
        jitter.observe(static_cast<std::uint64_t>(i), stick((i % 2 == 0) ? 1.0 : -1.0, 0.2), 0.0);
    CHECK(jitter.current().keep_heading_probability < 0.3);  // erratic mover is not predictable

    HumanIntentPredictor a;
    HumanIntentFrame hit;
    hit.attack_held = true;
    a.observe(0, HumanIntentFrame{}, 0.0);
    a.observe(1, hit, 0.0);
    CHECK(a.current().attack_started && a.current().kind == IntentKind::AttackHold);
    a.observe(2, hit, 0.0);
    CHECK(!a.current().attack_started && a.current().attack_run_ticks == 2);

    HumanIntentFrame nan_frame;
    nan_frame.move_x = std::nan("");
    nan_frame.look_dx = std::nan("");
    a.observe(3, nan_frame, 0.0);  // hostile / corrupt input is treated as no input
    CHECK(!a.current().moving && !a.current().flick);
}

void test_double_input_takeover() {
    Rig rig(UserMode::Takeover);
    AuthorityDecision d = rig.step(HumanIntentFrame{});
    CHECK(d.state == Authority::AiDriving && d.ai_moves && d.ai_looks && d.ai_acts);

    // First deliberate input (a camera turn): does NOT take control.
    rig.idle(4);
    d = rig.step(flick(25.0));
    CHECK(d.contest_registered && d.contest_count == 1);
    CHECK(d.state == Authority::HumanContesting && d.ai_moves);
    // Turn, then run the other way, as in "I turn to run": still only the first gesture while held.
    d = rig.step(stick(0.0, -1.0));
    CHECK(d.state == Authority::HumanContesting && !d.human_took_control);
    d = rig.step(stick(0.0, -1.0));
    CHECK(d.state == Authority::HumanContesting && d.ai_moves);
    CHECK(d.hint_heading_deg.has_value());

    // Release, then a SECOND separate deliberate input inside the window: human takes control.
    d = rig.idle(5);
    CHECK(d.state == Authority::HumanContesting);
    d = rig.step(stick(0.0, -1.0));
    CHECK(d.human_took_control && d.state == Authority::Assist && !d.ai_moves && !d.ai_looks);
    CHECK(d.contest_count == 2);

    // AI may not re-engage during the hold, even though it has a plan...
    for (int i = 0; i < 90; ++i) {
        d = rig.step(HumanIntentFrame{});
        CHECK(d.state == Authority::Assist && !d.ai_moves);
    }
    // ...and re-engages in Takeover mode once the hold has elapsed.
    d = rig.idle(20);
    CHECK(d.state == Authority::AiDriving && d.ai_moves);
}

void test_gesture_rules() {
    // A held input is ONE gesture: it never counts twice.
    Rig held(UserMode::Takeover);
    held.step(HumanIntentFrame{});
    AuthorityDecision d;
    for (int i = 0; i < 25; ++i) d = held.step(flick(30.0));
    CHECK(d.state == Authority::HumanContesting && d.contest_count == 1);
    // Window expiry: the AI keeps control and the contest is forgotten.
    d = held.idle(60);
    CHECK(d.state == Authority::AiDriving && d.contest_count == 0);
    // A new lone gesture is a first gesture again, not a takeover.
    d = held.step(flick(30.0));
    CHECK(d.state == Authority::HumanContesting && !d.human_took_control);

    // Two gestures further apart than the window never take control.
    Rig apart(UserMode::Takeover);
    apart.step(HumanIntentFrame{});
    apart.step(flick(30.0));
    apart.idle(50);
    d = apart.step(flick(30.0));
    CHECK(d.state != Authority::Assist && !d.human_took_control);

    // Agreeing with the AI is not a contest.
    Rig agree(UserMode::Takeover);
    agree.step(HumanIntentFrame{});
    for (int i = 0; i < 30; ++i) {
        d = agree.step(stick(0.0, 1.0));  // same heading as the AI (+Z)
        CHECK(d.state == Authority::AiDriving);
    }
}

void test_assist_reengage_by_intent() {
    Rig rig(UserMode::Assist);
    AuthorityDecision d = rig.idle(10);
    CHECK(d.state == Authority::Assist && !d.ai_moves);  // plan exists but the human hasn't started anything
    HumanIntentFrame hit;
    hit.attack_held = true;
    for (int i = 0; i < 6; ++i) d = rig.step(hit);
    CHECK(d.state == Authority::AiDriving);  // predicted attack intent -> AI follows through
    // Second gesture sequence still hands control back.
    d = rig.step(flick(40.0));
    CHECK(d.state == Authority::HumanContesting);
    rig.idle(5);
    d = rig.step(flick(40.0));
    CHECK(d.human_took_control);

    Rig no_plan(UserMode::Assist);
    no_plan.plan = false;
    for (int i = 0; i < 10; ++i) d = no_plan.step(hit);
    CHECK(d.state == Authority::Assist);  // no plan -> nothing to drive
}

void test_emergency_overlay() {
    // GuardOnly: the human drives, the AI only rescues.
    Rig rig(UserMode::GuardOnly);
    AuthorityDecision d = rig.step(stick(0.0, 1.0));
    CHECK(d.state == Authority::Human && !d.ai_moves);
    rig.emergency = true;
    d = rig.step(stick(0.0, 1.0));
    CHECK(d.state == Authority::Emergency && d.emergency && d.ai_moves && d.ai_acts);
    rig.emergency = false;
    for (int i = 0; i < 10; ++i) {  // latch
        d = rig.step(stick(0.0, 1.0));
        CHECK(d.state == Authority::Emergency);
    }
    d = rig.step(stick(0.0, 1.0));
    CHECK(d.state == Authority::Human);

    // Human takeover then danger: AI overrides, then the previous (human-owned) state is restored.
    Rig t(UserMode::Takeover);
    t.step(HumanIntentFrame{});
    t.step(flick(30.0));
    t.idle(5);
    d = t.step(flick(30.0));
    CHECK(d.human_took_control);
    t.emergency = true;  // human runs toward a void
    d = t.step(stick(0.0, 1.0));
    CHECK(d.state == Authority::Emergency && d.ai_moves);
    // Gestures during an emergency do not count.
    d = t.step(flick(50.0));
    CHECK(d.state == Authority::Emergency && !d.human_took_control);
    t.emergency = false;
    d = t.idle(12);
    CHECK(d.state == Authority::Assist);

    // Explicit override (P0) beats an emergency.
    Rig p0(UserMode::Takeover);
    p0.step(HumanIntentFrame{});
    p0.emergency = true;
    HumanIntentFrame button;
    button.explicit_override = true;
    d = p0.step(button);
    CHECK(d.state == Authority::Human && !d.ai_moves && !d.emergency && d.human_took_control);

    // Off means off, even in an emergency.
    Rig off(UserMode::Off);
    off.emergency = true;
    d = off.step(stick(0.0, 1.0));
    CHECK(d.state == Authority::Human && !d.ai_moves && !d.emergency);

    // Configurable: a user may disable survival override entirely.
    TakeoverConfig cfg;
    cfg.survival_overrides_human = false;
    Rig polite(UserMode::GuardOnly, cfg);
    polite.emergency = true;
    d = polite.step(stick(0.0, 1.0));
    CHECK(d.state == Authority::Human && !d.emergency);

    // panic_release switches the AI off.
    Rig panic(UserMode::Takeover);
    panic.step(HumanIntentFrame{});
    panic.authority.panic_release();
    d = panic.step(HumanIntentFrame{});
    CHECK(d.state == Authority::Human && !d.ai_moves);
}

// ---------------------------------------------------------------- learner

SkillContext ctx(const std::string& opp, const std::string& arch, const std::string& sit = "pvp.duel",
                 const std::string& server = "srv|bw") {
    SkillContext c;
    c.server_key = server;
    c.situation = sit;
    c.archetype = arch;
    c.opponent_id = opp;
    return c;
}
SkillOutcome outcome(const std::string& skill, const SkillContext& c, bool ok, std::uint64_t tick) {
    SkillOutcome o;
    o.skill = skill;
    o.context = c;
    o.success = ok;
    o.reward = ok ? 0.9 : 0.1;
    o.tick = tick;
    return o;
}

void test_failure_transfer() {
    SkillLearner L;
    const SkillContext A = ctx("A", "strafer");
    const SkillContext B = ctx("B", "strafer");
    const SkillContext C = ctx("C", "rusher");
    const SkillContext D = ctx("D", "camper", "pvp.duel", "other|server");
    const SkillContext E = ctx("E", "camper", "void.edge", "other|server");
    for (int i = 0; i < 6; ++i) L.record(outcome("pressure_chase", A, false, static_cast<std::uint64_t>(i) * 500U));
    const double mA = L.mean("pressure_chase", A);
    const double mB = L.mean("pressure_chase", B);
    const double mC = L.mean("pressure_chase", C);
    const double mD = L.mean("pressure_chase", D);
    const double mE = L.mean("pressure_chase", E);
    // Distrust is strongest for A, weaker for A-like players, weaker still for different players,
    // and almost nil for an unrelated server/situation.
    CHECK(mA < mB && mB < mC && mC < mD && mD < mE && mE <= 0.5);
    CHECK(mA < 0.25 && mB > 0.28 && mE > 0.45);
    // ...and B's own successes quickly overpower the weak cross-player prior: it CAN work for B.
    for (int i = 0; i < 3; ++i) L.record(outcome("pressure_chase", B, true, 4000U + static_cast<std::uint64_t>(i)));
    CHECK(L.mean("pressure_chase", B) > 0.5);
    CHECK(L.mean("pressure_chase", A) < 0.30);  // still bad against A
    // An untouched skill is unaffected by all of this.
    CHECK(std::fabs(L.mean("circle_strafe", A) - 0.5) < 1e-9);
}

void test_cooldown_ban_and_rotation() {
    SkillLearner L;
    const SkillContext A = ctx("A", "strafer");
    std::vector<SkillCandidate> cands = {{"pressure_chase", 0.5, 0.60}, {"circle_strafe", 0.5, 0.58}, {"kite_back", 0.5, 0.55}};
    SkillChoice first = L.choose(cands, A, 100);
    // Whatever it chose fails: it must not be repeated immediately against the same opponent.
    L.record(outcome(first.skill, A, false, 100));
    SkillChoice second = L.choose(cands, A, 101);
    CHECK(second.skill != first.skill);
    // Cooldown expires.
    const std::vector<SkillChoice> later = L.rank(cands, A, 100 + 200);
    bool first_listed_on_cooldown = false;
    for (const SkillChoice& c : later)
        if (c.skill == first.skill && c.on_cooldown) first_listed_on_cooldown = true;
    CHECK(!first_listed_on_cooldown);
    // Three failures in a row ban it for a long time...
    L.record(outcome(first.skill, A, false, 300));
    L.record(outcome(first.skill, A, false, 500));
    bool banned = false;
    for (const SkillChoice& c : L.rank(cands, A, 600))
        if (c.skill == first.skill && c.banned) banned = true;
    CHECK(banned);
    CHECK(L.choose(cands, A, 600).skill != first.skill);
    // ...but only against that opponent: a different opponent is not affected by the ban.
    const SkillContext B = ctx("B", "rusher");
    for (const SkillChoice& c : L.rank(cands, B, 600)) CHECK(!c.banned && !c.on_cooldown);
    // The ban lapses eventually.
    for (const SkillChoice& c : L.rank(cands, A, 600 + 5000)) CHECK(!c.banned);
}

void test_no_single_skill_lock_in() {
    SkillLearner L;
    const SkillContext c = ctx("A", "strafer");
    const std::vector<SkillCandidate> cands = {{"s1", 0.5, 0.5}, {"s2", 0.5, 0.5}, {"s3", 0.5, 0.5}};
    const double truth[3] = {0.80, 0.62, 0.30};
    std::uint64_t lcg = 12345;
    auto roll = [&lcg]() {
        lcg = lcg * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(lcg >> 11U) / 9007199254740992.0;
    };
    std::map<std::string, int> used;
    for (std::uint64_t t = 0; t < 600; ++t) {
        const SkillChoice pick = L.choose(cands, c, t * 100U);  // spaced past cooldowns
        const std::size_t i = pick.skill == "s1" ? 0U : (pick.skill == "s2" ? 1U : 2U);
        used[pick.skill] += 1;
        const bool ok = roll() < truth[i];
        L.record(outcome(pick.skill, c, ok, t * 100U));
    }
    CHECK(used["s1"] > used["s2"] && used["s2"] > used["s3"]);        // quality is respected
    CHECK(used["s1"] < 480);                                           // never a monopoly
    CHECK(used["s2"] > 60 && used["s3"] > 5);                          // alternatives stay alive
    CHECK(L.mean("s1", c) > L.mean("s2", c) && L.mean("s2", c) > L.mean("s3", c));

    // When the world changes (s1 stops working), the learner adapts away from it.
    int s1_after = 0;
    for (std::uint64_t t = 600; t < 900; ++t) {
        const SkillChoice pick = L.choose(cands, c, t * 100U);
        const bool ok = pick.skill == "s1" ? roll() < 0.05 : roll() < (pick.skill == "s2" ? 0.7 : 0.3);
        if (pick.skill == "s1") ++s1_after;
        L.record(outcome(pick.skill, c, ok, t * 100U));
    }
    CHECK(s1_after < 150);
}

void test_critical_context_no_exploration() {
    SkillLearner L;
    SkillContext c = ctx("", "", "void.edge");
    c.critical = true;
    for (int i = 0; i < 5; ++i) L.record(outcome("water_clutch", c, true, static_cast<std::uint64_t>(i)));
    const std::vector<SkillCandidate> cands = {{"water_clutch", 0.5, 0.5}, {"untested_trick", 0.5, 0.5}};
    for (std::uint64_t t = 1000; t < 1040; ++t) CHECK(L.choose(cands, c, t).skill == "water_clutch");
}

void test_persistence_and_bounds() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "bedrockai_v35_learner";
    fs::remove_all(dir);
    SkillLearner L;
    const SkillContext A = ctx("A", "strafer");
    for (int i = 0; i < 4; ++i) L.record(outcome("x", A, i % 2 == 0, static_cast<std::uint64_t>(i) * 300U));
    const std::string path = (dir / "learner.tsv").string();
    std::string err;
    CHECK(L.save(path, &err));
    CHECK(L.save(path, &err));  // second save keeps a rollback copy
    CHECK(fs::exists(path + ".bak"));
    SkillLearner R;
    CHECK(R.load(path, &err));
    CHECK(std::fabs(R.mean("x", A) - L.mean("x", A)) < 1e-9);
    CHECK(R.entries() == L.entries());

    {   // corrupted / hostile files are rejected without touching state
        std::ofstream bad(dir / "bad.tsv", std::ios::binary);
        bad << "BedrockAiSkillLearner\tv1\nB\tG|x\t-5\t1\n";
    }
    SkillLearner S;
    CHECK(!S.load((dir / "bad.tsv").string(), &err));
    CHECK(S.entries() == 0U);
    {
        std::ofstream bad(dir / "nan.tsv", std::ios::binary);
        bad << "BedrockAiSkillLearner\tv1\nB\tG|x\tnan\t1\n";
    }
    CHECK(!S.load((dir / "nan.tsv").string(), &err));
    {
        std::ofstream bad(dir / "hdr.tsv", std::ios::binary);
        bad << "SomethingElse\tv1\n";
    }
    CHECK(!S.load((dir / "hdr.tsv").string(), &err));
    CHECK(!S.load((dir / "missing.tsv").string(), &err));

    SkillLearnerConfig tiny;
    tiny.max_entries = 40;
    SkillLearner T(tiny);
    for (int i = 0; i < 500; ++i) T.record(outcome("x", ctx("opp" + std::to_string(i), "a"), false, 1));
    CHECK(T.entries() <= 40U);  // memory is bounded no matter how many opponents are seen
    fs::remove_all(dir);
}

void test_determinism() {
    SkillLearner a, b;
    const SkillContext c = ctx("A", "x");
    const std::vector<SkillCandidate> cands = {{"p", 0.5, 0.5}, {"q", 0.5, 0.5}, {"r", 0.5, 0.5}};
    for (std::uint64_t t = 0; t < 50; ++t) CHECK(a.choose(cands, c, t * 10U).skill == b.choose(cands, c, t * 10U).skill);
}

} // namespace

int main() {
    test_predictor();
    test_double_input_takeover();
    test_gesture_rules();
    test_assist_reengage_by_intent();
    test_emergency_overlay();
    test_failure_transfer();
    test_cooldown_ban_and_rotation();
    test_no_single_skill_lock_in();
    test_critical_context_no_exploration();
    test_persistence_and_bounds();
    test_determinism();
    std::cout << "v35 tests passed\n";
    return 0;
}
