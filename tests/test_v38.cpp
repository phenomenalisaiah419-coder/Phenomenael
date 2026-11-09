#include "sim_rig.hpp"

#include "core/technique_registry.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace bedrockai;
using namespace simrig;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAIL: " << #condition << " at line " << __LINE__ << "\n"; \
    std::exit(1); } } while (false)

namespace {

void test_registry() {
    const TechniqueRegistry& reg = TechniqueRegistry::instance();
    std::string err;
    CHECK(reg.validate(&err));
    const TechniqueRegistry::Counts c = reg.counts();
    CHECK(c.total >= 150 && c.executable >= 18 && c.scripted >= 30 && c.planned >= 40 && c.blocked >= 10);
    CHECK(c.executable + c.scripted + c.planned + c.blocked == c.total);

    // Policy-forbidden things stay forbidden, whatever the request.
    const char* blocked[] = {"policy.bed_destruction", "policy.economy_automation", "exploit.reach_extension",
                             "exploit.anti_cheat_bypass", "exploit.identity_spoofing", "exploit.hidden_automation",
                             "exploit.packet_duplication", "exploit.fly_noclip"};
    for (const char* id : blocked) CHECK(reg.blocked_by_policy(id));
    CHECK(!reg.blocked_by_policy("combat.strafe_orbit") && !reg.blocked_by_policy("does.not.exist"));
    for (const TechniqueSpec* s : reg.filter(kBothEditions, kModeAny, Maturity::Planned)) CHECK(s->maturity != Maturity::Blocked);

    // Edition awareness: Java-only techniques are not offered to Bedrock and vice versa.
    bool block_hit_for_bedrock = false;
    for (const TechniqueSpec* s : reg.filter(kBedrock, kModeAny, Maturity::Planned))
        block_hit_for_bedrock = block_hit_for_bedrock || std::string(s->id) == "combat.block_hit";
    CHECK(!block_hit_for_bedrock);
    bool block_hit_for_java = false;
    for (const TechniqueSpec* s : reg.filter(kJava, kModeAny, Maturity::Planned))
        block_hit_for_java = block_hit_for_java || std::string(s->id) == "combat.block_hit";
    CHECK(block_hit_for_java);
    CHECK(reg.filter(kBedrock, kModeBedWars, Maturity::Executable).size() >= 8U);

    // Everything the guardian / hub can actually emit is catalogued as Executable.
    const char* emitted[] = {"survival.sneak_halt", "survival.step_back", "survival.bridge_forward", "survival.block_clutch",
                             "survival.water_clutch", "survival.pearl_escape", "combat.strafe_orbit", "combat.pressure_chase",
                             "combat.kite_back", "combat.retreat_reset", "bridge.sneak_straight", "bridge.sneak_diagonal"};
    for (const char* id : emitted) {
        const TechniqueSpec* s = reg.find(id);
        CHECK(s != nullptr && s->maturity == Maturity::Executable);
    }
    // ...and nothing that depends on unmeasured physics is called executable (except the self-refusing pearl).
    for (const TechniqueSpec& s : reg.all())
        if (s.maturity == Maturity::Executable && s.needs_calibration) CHECK(std::string(s.id) == "survival.pearl_escape");
}

void test_policy_and_editions() {
    CHECK(AutomationPolicy::default_ceiling(ServerClass::OwnWorld) == AssistLevel::Takeover);
    CHECK(AutomationPolicy::default_ceiling(ServerClass::PrivateRealm) == AssistLevel::Takeover);
    CHECK(AutomationPolicy::default_ceiling(ServerClass::PracticeServer) == AssistLevel::Assist);
    CHECK(AutomationPolicy::default_ceiling(ServerClass::PublicCompetitive) == AssistLevel::ObserveOnly);
    CHECK(AutomationPolicy::default_ceiling(ServerClass::Unknown) == AssistLevel::ObserveOnly);
    AutomationPolicy p;
    std::string err;
    CHECK(!p.grant(AutomationGrant{"", AssistLevel::Assist, true}, &err));
    CHECK(!p.grant(AutomationGrant{"srv", AssistLevel::Assist, false}, &err));  // needs the user's confirmation
    CHECK(p.grant(AutomationGrant{"srv", AssistLevel::Assist, true}, &err));
    CHECK(p.effective("srv", ServerClass::PublicCompetitive, AssistLevel::Takeover) == AssistLevel::Assist);
    CHECK(p.effective("other", ServerClass::PublicCompetitive, AssistLevel::Takeover) == AssistLevel::ObserveOnly);
    CHECK(p.effective("srv", ServerClass::PublicCompetitive, AssistLevel::Off) == AssistLevel::Off);  // user can always turn it off
    p.revoke("srv");
    CHECK(p.effective("srv", ServerClass::PublicCompetitive, AssistLevel::Takeover) == AssistLevel::ObserveOnly);
    CHECK(AutomationPolicy::to_user_mode(AssistLevel::ObserveOnly) == UserMode::Off);
    CHECK(AutomationPolicy::to_user_mode(AssistLevel::SurvivalOnly) == UserMode::GuardOnly);

    const EditionProfile bed = bedrock_profile();
    const EditionProfile jav = java_profile();
    const EditionProfile crk = java_profile(AuthMode::OfflineCracked);
    CHECK(bed.edition == GameEdition::Bedrock && bed.needs_login_cache && bed.touch_controls);
    CHECK(jav.wire == WireStatus::NotImplemented && crk.wire == WireStatus::NotImplemented);  // honest: no Java wire codec yet
    CHECK(!crk.needs_login_cache && crk.auth == AuthMode::OfflineCracked);
    CHECK(!bedrock_profile(AuthMode::OfflineCracked).needs_login_cache);
    CHECK(edition_matches(kBedrock, GameEdition::Bedrock) && !edition_matches(kBedrock, GameEdition::Java));
    CHECK(edition_matches(kBothEditions, GameEdition::Java));
}

SimEnemy camper(const std::string& id, double x, double z) {
    SimEnemy e;
    e.id = id;
    e.style = EnemyStyle::Camper;
    e.pos = Vec3{x, 64.0, z};
    e.damage = 0.0;
    return e;
}

void test_each_combat_technique_executes() {
    const char* techs[] = {"combat.pressure_chase", "combat.strafe_orbit", "combat.kite_back"};
    for (const char* tech : techs) {
        Rig r;
        platform(r.world.grid, 0, 0, 15, 15);
        r.world.player.yaw = 0.0;
        r.world.enemies.push_back(camper("A", 3.5, 9.5));
        r.human = [](std::uint64_t) {
            HumanIntentFrame f;
            f.attack_held = true;
            return f;
        };
        // Distrust the alternatives against this opponent so the learner must pick `tech`.
        for (const char* other : techs) {
            if (std::string(other) == tech) continue;
            for (int i = 0; i < 4; ++i) {
                SkillOutcome o;
                o.skill = other;
                o.context.server_key = "sim|test";
                o.context.situation = "pvp.duel";
                o.context.opponent_id = "A";
                o.success = false;
                o.reward = 0.1;
                o.tick = 0;
                r.hub.learner().record(o);
            }
        }
        r.run(150);
        CHECK(r.techniques.count(tech) == 1U);
        CHECK(r.world.damage_dealt >= 6.0);
        CHECK(!r.world.player.dead && r.world.rejected.empty());
    }
}

void test_retreat_when_hurt() {
    Rig r;
    platform(r.world.grid, 0, 0, 15, 15);
    r.world.player.pos = Vec3{12.5, 64.0, 8.5};
    r.world.player.yaw = 90.0;
    r.world.player.health = 5.0;  // 25%: below the retreat threshold
    SimEnemy e;
    e.id = "R";
    e.style = EnemyStyle::Rusher;
    e.pos = Vec3{6.5, 64.0, 8.5};
    e.damage = 1.0;
    r.world.enemies.push_back(e);
    r.human = [](std::uint64_t) {
        HumanIntentFrame f;
        f.attack_held = true;
        return f;
    };
    const double d0 = 6.0;
    r.run(14);
    CHECK(r.techniques.count("combat.retreat_reset") == 1U);
    const double d1 = std::hypot(r.world.enemies[0].pos.x - r.world.player.pos.x, r.world.enemies[0].pos.z - r.world.player.pos.z);
    CHECK(d1 > d0 - 0.5);       // it opened or held the gap instead of closing it
    CHECK(r.world.damage_dealt == 0.0);  // retreat never swings
    CHECK(!r.world.player.dead);
}

void test_learning_from_the_humans_second_input() {
    // Every time the human takes the character back, the running technique is penalised, so the
    // next engagement does not blindly repeat it: a collection of skills, never one forever.
    Rig r;
    platform(r.world.grid, 0, 0, 15, 15);
    r.world.player.yaw = 0.0;
    r.world.enemies.push_back(camper("A", 3.5, 13.5));
    const std::uint64_t kCycle = 170;
    r.human = [&](std::uint64_t t) {
        HumanIntentFrame f;
        const std::uint64_t k = t % kCycle;
        if (k == 20 || k == 28) f.look_dx = 40.0;  // two separate turns = a change of plan
        return f;
    };
    std::vector<std::string> engagements;
    Authority prev = Authority::Assist;
    for (int i = 0; i < 6 * static_cast<int>(kCycle); ++i) {
        r.step();
        if (r.last.authority.state == Authority::AiDriving && prev != Authority::AiDriving && prev != Authority::HumanContesting &&
            !r.last.technique.empty())
            engagements.push_back(r.last.technique);
        prev = r.last.authority.state;
    }
    CHECK(engagements.size() >= 4U);
    std::set<std::string> distinct(engagements.begin(), engagements.end());
    CHECK(distinct.size() >= 3U);
    std::map<std::string, int> use_count;
    for (const std::string& e : engagements) ++use_count[e];
    for (const auto& kv : use_count) CHECK(kv.second * 3 <= static_cast<int>(engagements.size()) * 2);  // no technique takes >2/3 of the engagements
    // The learner has recorded the human's veto as evidence (mean below the untouched prior).
    SkillContext c;
    c.server_key = "sim|test";
    c.situation = "pvp.duel";
    c.opponent_id = "A";
    int distrusted = 0;
    for (const std::string& t : distinct)
        if (r.hub.learner().mean(t, c) < 0.5) ++distrusted;
    CHECK(distrusted >= 2);
}

void test_no_penalty_without_human_attack() {
    // No attack input from the human: the AI cannot swing, so "no contact" must not be held against a technique.
    Rig r;
    platform(r.world.grid, 0, 0, 15, 15);
    r.world.player.yaw = 0.0;
    r.world.enemies.push_back(camper("A", 3.5, 9.5));
    r.run(400);
    CHECK(r.hub.learner().entries() == 0U);
    CHECK(r.world.damage_dealt == 0.0);
}

void test_persistence_through_the_hub() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "bedrockai_v38";
    fs::remove_all(dir);
    ControlHub a;
    SkillOutcome o;
    o.skill = "combat.kite_back";
    o.context.server_key = "s";
    o.context.situation = "pvp.duel";
    o.context.opponent_id = "X";
    o.success = false;
    o.reward = 0.1;
    a.learner().record(o);
    std::string err;
    const std::string path = (dir / "skills.tsv").string();
    CHECK(a.learner().save(path, &err));
    ControlHub b;
    CHECK(b.learner().load(path, &err));
    CHECK(std::fabs(a.learner().mean("combat.kite_back", o.context) - b.learner().mean("combat.kite_back", o.context)) < 1e-9);
    fs::remove_all(dir);
}

void test_adversarial_survival_rate() {
    // Hostile human scripts: hop-spam, random strafing, random camera flicks, empty or small kits,
    // random lava.  The guardian has to keep the character alive in >= 99% of 200 runs.
    // (Not 100%: see docs/AUDIT_V035.md, "residual risk".)
    std::uint64_t s = 424242;
    auto rnd = [&s]() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(s >> 11U) / 9007199254740992.0;
    };
    int deaths = 0;
    const int runs = 200;
    for (int n = 0; n < runs; ++n) {
        Rig r;
        r.level = AssistLevel::SurvivalOnly;
        const int w = 5 + static_cast<int>(rnd() * 10.0);
        const int d = 5 + static_cast<int>(rnd() * 10.0);
        platform(r.world.grid, 0, 0, w, d);
        r.world.player.pos = Vec3{0.5 + rnd() * w, 64.0, 0.5 + rnd() * d};
        if (rnd() < 0.3) {
            const int lx = static_cast<int>(rnd() * w);
            const int lz = static_cast<int>(rnd() * d);
            if (std::abs(lx - static_cast<int>(r.world.player.pos.x)) > 2 || std::abs(lz - static_cast<int>(r.world.player.pos.z)) > 2)
                r.world.grid.set(lx, 64, lz, BlockClass::Lava);
        }
        r.world.player.yaw = std::floor(rnd() * 8.0) * 45.0;
        r.world.player.blocks = rnd() < 0.5 ? 0 : static_cast<int>(rnd() * 12.0);
        double fwd = 1.0, str = 0.0;
        bool sprint = true, jump = false;
        r.human = [&](std::uint64_t t) {
            if (t % 11 == 0) {
                fwd = std::floor(rnd() * 3.0) - 1.0;
                str = std::floor(rnd() * 3.0) - 1.0;
                sprint = rnd() < 0.7;
                jump = rnd() < 0.35;
            }
            HumanIntentFrame f;
            f.move_y = fwd;
            f.move_x = str;
            f.sprint_held = sprint;
            f.jump_held = jump;
            f.look_dx = (t % 25 == 0) ? (rnd() * 180.0 - 90.0) : 0.0;
            return f;
        };
        r.run(300);
        if (r.world.player.dead) ++deaths;
        CHECK(r.world.rejected.size() < 5U);  // plans are valid: the simulator rarely has to refuse them
    }
    std::cout << "adversarial runs=" << runs << " deaths=" << deaths << "\n";
    CHECK(deaths <= 2);
}

} // namespace

int main() {
    test_registry();
    test_policy_and_editions();
    test_each_combat_technique_executes();
    test_retreat_when_hurt();
    test_learning_from_the_humans_second_input();
    test_no_penalty_without_human_attack();
    test_persistence_through_the_hub();
    test_adversarial_survival_rate();
    std::cout << "v38 tests passed\n";
    return 0;
}
