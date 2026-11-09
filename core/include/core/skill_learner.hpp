#pragma once

// Skill learning (v0.35): "a collection of skills, never one skill forever".
//
// Replaces the single global (attempts, successes) tally of SkillExperienceBook with a
// contextual Beta-posterior bandit:
//
//   * Evidence is kept at five levels: opponent, opponent-archetype, situation, server, global.
//   * An outcome is written at every level but with attenuated weight upward
//     (opponent 1.0 > archetype 0.25 > situation 0.10 > server 0.04 > global 0.02),
//     so a skill that fails against player A is strongly distrusted against A, mildly against
//     A-like players, and barely at all against a different kind of player B.
//     B's own outcomes then overpower that weak prior after a couple of trials.
//   * Selection is Thompson sampling (probability-matching), so several good skills share the
//     workload and a skill with little evidence still gets tried when it is safe to explore.
//   * Failure cooldown + failure-streak ban: after a failure the same skill is suppressed for
//     a while against that opponent; after N consecutive failures it is banned for a long while.
//   * Repetition penalty: a skill used many times in a row must keep clearly winning to continue.
//   * Survival-critical contexts never explore (exploit the best mean), but still honour
//     cooldowns and bans so a failed rescue is not blindly repeated.
//   * Outcomes must come from observed results, never from "the packet was sent".
//
// Everything is deterministic given the seed, bounded in memory, and persisted atomically.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bedrockai {

struct SkillContext {
    std::string server_key;    // e.g. "play.example.net:19132|bedwars"
    std::string situation;     // e.g. "pvp.duel", "void.edge", "bridge.gap"
    std::string archetype;     // opponent archetype, may be empty
    std::string opponent_id;   // stable opponent identity, may be empty
    bool critical = false;     // survival-critical: no exploration
};

struct SkillOutcome {
    std::string skill;
    SkillContext context;
    bool success = false;
    double reward = 0.0;       // 0..1 measured quality of the outcome
    std::uint64_t tick = 0;
};

struct SkillCandidate {
    std::string skill;
    double prior = 0.5;        // catalogue prior for this skill in [0,1]
    double base_score = 0.5;   // rule-engine suitability for the current situation in [0,1]
};

struct SkillChoice {
    std::string skill;
    double score = 0.0;
    double mean = 0.5;
    double sampled = 0.5;
    double evidence = 0.0;     // effective number of observations behind the mean
    bool on_cooldown = false;
    bool banned = false;
    std::string why;
};

struct SkillLearnerConfig {
    double prior_strength = 3.0;
    double decay_opponent = 0.97;   // forgetting per update at each level
    double decay_coarse = 0.995;
    double transfer_archetype = 0.25;
    double transfer_situation = 0.10;
    double transfer_server = 0.04;
    double transfer_global = 0.02;
    int cooldown_ticks = 80;
    int ban_streak = 3;
    int ban_ticks = 1200;
    double cooldown_penalty = 0.6;
    double ban_penalty = 2.0;
    double repeat_penalty = 0.07;
    double repeat_cap = 0.28;
    double explore_bonus = 0.12;
    double sample_weight = 1.0;
    std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
    std::size_t max_entries = 20000;
};

class SkillLearner {
public:
    explicit SkillLearner(SkillLearnerConfig config = {});

    void record(const SkillOutcome& outcome);
    double mean(const std::string& skill, const SkillContext& ctx, double prior = 0.5) const;
    double evidence(const std::string& skill, const SkillContext& ctx) const;

    // Deterministic ranking by posterior mean (no sampling, no state change): for explanations.
    std::vector<SkillChoice> rank(const std::vector<SkillCandidate>& candidates, const SkillContext& ctx,
                                  std::uint64_t tick) const;
    // Thompson-sampling choice. Remembers the pick for the repetition penalty.
    SkillChoice choose(const std::vector<SkillCandidate>& candidates, const SkillContext& ctx,
                       std::uint64_t tick);

    bool save(const std::string& path, std::string* error = nullptr) const;
    bool load(const std::string& path, std::string* error = nullptr);
    std::size_t entries() const { return beta_.size() + fails_.size(); }
    void reset();

private:
    struct Beta {
        double a = 0.0;
        double b = 0.0;
    };
    struct FailState {
        std::uint64_t last_fail_tick = 0;
        int streak = 0;
    };
    SkillLearnerConfig cfg_;
    std::unordered_map<std::string, Beta> beta_;
    std::unordered_map<std::string, FailState> fails_;
    std::vector<std::string> recent_;
    mutable std::uint64_t rng_state_;

    void accumulate(const std::string& key, double weight, double decay, double reward);
    void posterior(const std::string& skill, const SkillContext& ctx, double prior, double* alpha,
                   double* beta) const;
    std::vector<SkillChoice> evaluate(const std::vector<SkillCandidate>& candidates, const SkillContext& ctx,
                                      std::uint64_t tick, bool sample, bool use_recent) const;
    std::uint64_t next_u64() const;
    double uniform() const;
    double normal() const;
    double gamma(double shape) const;
    double beta_sample(double a, double b) const;
};

} // namespace bedrockai
