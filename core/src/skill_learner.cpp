#include "core/skill_learner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

namespace bedrockai {
namespace {

constexpr std::uintmax_t kMaxFileBytes = 4U * 1024U * 1024U;
constexpr std::size_t kRecentCap = 8;

std::string clean(const std::string& s) {
    std::string out = s.substr(0, 96);
    for (char& c : out)
        if (c == '|' || c == '\t' || c == '\n' || c == '\r') c = '_';
    return out;
}
double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

std::string key_global(const std::string& k) { return "G|" + k; }
std::string key_server(const std::string& k, const SkillContext& c) { return "S|" + k + "|" + clean(c.server_key); }
std::string key_situation(const std::string& k, const SkillContext& c) { return "C|" + k + "|" + clean(c.situation); }
std::string key_archetype(const std::string& k, const SkillContext& c) { return "A|" + k + "|" + clean(c.archetype); }
std::string key_opponent(const std::string& k, const SkillContext& c) {
    return "O|" + k + "|" + clean(c.server_key) + "|" + clean(c.opponent_id);
}
std::string key_fail(const std::string& k, const SkillContext& c) {
    return "F|" + k + "|" + clean(c.server_key) + "|" +
           (c.opponent_id.empty() ? "sit:" + clean(c.situation) : "opp:" + clean(c.opponent_id));
}

} // namespace

SkillLearner::SkillLearner(SkillLearnerConfig config) : cfg_(config), rng_state_(config.seed ? config.seed : 1U) {}

void SkillLearner::reset() {
    beta_.clear();
    fails_.clear();
    recent_.clear();
    rng_state_ = cfg_.seed ? cfg_.seed : 1U;
}

std::uint64_t SkillLearner::next_u64() const {
    rng_state_ ^= rng_state_ >> 12U;
    rng_state_ ^= rng_state_ << 25U;
    rng_state_ ^= rng_state_ >> 27U;
    return rng_state_ * 2685821657736338717ULL;
}
double SkillLearner::uniform() const {
    return (static_cast<double>(next_u64() >> 11U) + 0.5) * (1.0 / 9007199254740992.0);
}
double SkillLearner::normal() const {
    const double u1 = uniform();
    const double u2 = uniform();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}
double SkillLearner::gamma(double shape) const {
    if (!(shape > 0.0)) return 0.0;
    if (shape < 1.0) return gamma(shape + 1.0) * std::pow(uniform(), 1.0 / shape);
    const double d = shape - 1.0 / 3.0;
    const double c = 1.0 / std::sqrt(9.0 * d);
    for (int i = 0; i < 200; ++i) {
        const double x = normal();
        double v = 1.0 + c * x;
        if (v <= 0.0) continue;
        v = v * v * v;
        const double u = uniform();
        if (u < 1.0 - 0.0331 * x * x * x * x) return d * v;
        if (std::log(u) < 0.5 * x * x + d * (1.0 - v + std::log(v))) return d * v;
    }
    return d;
}
double SkillLearner::beta_sample(double a, double b) const {
    const double x = gamma(a);
    const double y = gamma(b);
    return (x + y) > 0.0 ? x / (x + y) : 0.5;
}

void SkillLearner::accumulate(const std::string& key, double weight, double decay, double reward) {
    auto it = beta_.find(key);
    if (it == beta_.end()) {
        if (beta_.size() + fails_.size() >= cfg_.max_entries) return;  // bounded memory
        it = beta_.emplace(key, Beta{}).first;
    }
    it->second.a = it->second.a * decay + weight * reward;
    it->second.b = it->second.b * decay + weight * (1.0 - reward);
}

void SkillLearner::record(const SkillOutcome& o) {
    if (o.skill.empty()) return;
    const std::string skill = clean(o.skill);
    double reward = std::isfinite(o.reward) ? clamp01(o.reward) : (o.success ? 0.7 : 0.2);
    reward = o.success ? std::max(reward, 0.5) : std::min(reward, 0.5);
    const SkillContext& c = o.context;
    const bool has_opponent = !c.opponent_id.empty();
    accumulate(key_global(skill), cfg_.transfer_global, cfg_.decay_coarse, reward);
    if (!c.server_key.empty()) accumulate(key_server(skill, c), cfg_.transfer_server, cfg_.decay_coarse, reward);
    if (!c.situation.empty())
        accumulate(key_situation(skill, c), has_opponent ? cfg_.transfer_situation : 1.0, cfg_.decay_coarse, reward);
    if (!c.archetype.empty())
        accumulate(key_archetype(skill, c), cfg_.transfer_archetype, cfg_.decay_coarse, reward);
    if (has_opponent) accumulate(key_opponent(skill, c), 1.0, cfg_.decay_opponent, reward);

    const std::string fk = key_fail(skill, c);
    auto it = fails_.find(fk);
    if (it == fails_.end()) {
        if (beta_.size() + fails_.size() >= cfg_.max_entries) return;
        it = fails_.emplace(fk, FailState{}).first;
    }
    if (o.success) {
        it->second.streak = 0;
    } else {
        it->second.streak = std::min(it->second.streak + 1, 1000000);
        it->second.last_fail_tick = o.tick;
    }
}

void SkillLearner::posterior(const std::string& raw_skill, const SkillContext& c, double prior, double* alpha,
                             double* beta) const {
    const std::string skill = clean(raw_skill);
    const double p = clamp01(prior);
    double a = cfg_.prior_strength * p;
    double b = cfg_.prior_strength * (1.0 - p);
    auto add = [&](const std::string& key) {
        const auto it = beta_.find(key);
        if (it != beta_.end()) {
            a += it->second.a;
            b += it->second.b;
        }
    };
    add(key_global(skill));
    if (!c.server_key.empty()) add(key_server(skill, c));
    if (!c.situation.empty()) add(key_situation(skill, c));
    if (!c.archetype.empty()) add(key_archetype(skill, c));
    if (!c.opponent_id.empty()) add(key_opponent(skill, c));
    *alpha = std::max(a, 1e-6);
    *beta = std::max(b, 1e-6);
}

double SkillLearner::mean(const std::string& skill, const SkillContext& ctx, double prior) const {
    double a = 0.0;
    double b = 0.0;
    posterior(skill, ctx, prior, &a, &b);
    return a / (a + b);
}

double SkillLearner::evidence(const std::string& skill, const SkillContext& ctx) const {
    double a = 0.0;
    double b = 0.0;
    posterior(skill, ctx, 0.5, &a, &b);
    return std::max(0.0, a + b - cfg_.prior_strength);
}

std::vector<SkillChoice> SkillLearner::evaluate(const std::vector<SkillCandidate>& candidates,
                                                const SkillContext& ctx, std::uint64_t tick, bool sample,
                                                bool use_recent) const {
    std::vector<SkillChoice> out;
    out.reserve(candidates.size());
    for (const SkillCandidate& cand : candidates) {
        if (cand.skill.empty()) continue;
        SkillChoice c;
        c.skill = cand.skill;
        double a = 0.0;
        double b = 0.0;
        posterior(cand.skill, ctx, cand.prior, &a, &b);
        c.mean = a / (a + b);
        c.evidence = std::max(0.0, a + b - cfg_.prior_strength);
        c.sampled = sample ? beta_sample(a, b) : c.mean;
        c.score = clamp01(cand.base_score) + cfg_.sample_weight * (c.sampled - 0.5);

        std::ostringstream why;
        why.precision(3);
        why << "mean=" << c.mean << " evidence=" << c.evidence << " base=" << cand.base_score;
        const auto fit = fails_.find(key_fail(clean(cand.skill), ctx));
        if (fit != fails_.end() && (fit->second.streak > 0)) {
            const std::uint64_t age = tick >= fit->second.last_fail_tick ? tick - fit->second.last_fail_tick
                                                                          : std::numeric_limits<std::uint64_t>::max();
            if (fit->second.streak >= cfg_.ban_streak && age < static_cast<std::uint64_t>(cfg_.ban_ticks)) {
                c.banned = true;
                c.score -= cfg_.ban_penalty;
                why << " [banned: " << fit->second.streak << " failures in a row]";
            } else if (age < static_cast<std::uint64_t>(cfg_.cooldown_ticks)) {
                c.on_cooldown = true;
                c.score -= cfg_.cooldown_penalty;
                why << " [cooldown after recent failure]";
            }
        }
        if (!ctx.critical && use_recent) {
            int run = 0;
            for (auto it = recent_.rbegin(); it != recent_.rend() && *it == cand.skill; ++it) ++run;
            if (run > 0) {
                c.score -= std::min(cfg_.repeat_cap, cfg_.repeat_penalty * static_cast<double>(run));
                why << " [repeat x" << run << "]";
            }
        }
        if (!ctx.critical && c.evidence < 0.5) {
            c.score += cfg_.explore_bonus;
            why << " [untested: explore]";
        }
        c.why = why.str();
        out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(), [](const SkillChoice& l, const SkillChoice& r) {
        if (l.score != r.score) return l.score > r.score;
        return l.skill < r.skill;
    });
    return out;
}

std::vector<SkillChoice> SkillLearner::rank(const std::vector<SkillCandidate>& candidates,
                                            const SkillContext& ctx, std::uint64_t tick) const {
    return evaluate(candidates, ctx, tick, false, false);
}

SkillChoice SkillLearner::choose(const std::vector<SkillCandidate>& candidates, const SkillContext& ctx,
                                 std::uint64_t tick) {
    const std::vector<SkillChoice> ranked = evaluate(candidates, ctx, tick, !ctx.critical, true);
    if (ranked.empty()) return SkillChoice{};
    SkillChoice best = ranked.front();
    for (const SkillChoice& c : ranked) {
        if (!c.banned) {
            best = c;
            break;
        }
    }
    if (best.banned) best.why += " [all candidates banned: least-bad pick]";
    recent_.push_back(best.skill);
    if (recent_.size() > kRecentCap) recent_.erase(recent_.begin());
    return best;
}

bool SkillLearner::save(const std::string& path, std::string* error) const {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    if (path.empty()) return fail("skill-learner path is empty");
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path target(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = fs::path(path + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail("cannot create skill-learner temporary file");
        out.precision(17);
        out << "BedrockAiSkillLearner\tv1\n";
        for (const auto& kv : beta_) out << "B\t" << kv.first << '\t' << kv.second.a << '\t' << kv.second.b << '\n';
        for (const auto& kv : fails_)
            out << "F\t" << kv.first << '\t' << kv.second.last_fail_tick << '\t' << kv.second.streak << '\n';
        out.flush();
        if (!out) return fail("failed writing skill-learner data");
    }
    fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    if (fs::exists(target, ec)) fs::copy_file(target, fs::path(path + ".bak"), fs::copy_options::overwrite_existing, ec);
    fs::rename(tmp, target, ec);
    if (ec) return fail("cannot commit skill-learner file: " + ec.message());
    return true;
}

bool SkillLearner::load(const std::string& path, std::string* error) {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto size = fs::file_size(fs::path(path), ec);
    if (ec) return fail("cannot stat skill-learner file");
    if (size > kMaxFileBytes) return fail("skill-learner file exceeds size limit");
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail("cannot open skill-learner file");
    std::string line;
    if (!std::getline(in, line) || line != "BedrockAiSkillLearner\tv1") return fail("skill-learner header/version invalid");
    std::unordered_map<std::string, Beta> beta;
    std::unordered_map<std::string, FailState> fails;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> f;
        std::size_t start = 0;
        for (;;) {
            const std::size_t pos = line.find('\t', start);
            if (pos == std::string::npos) {
                f.push_back(line.substr(start));
                break;
            }
            f.push_back(line.substr(start, pos - start));
            start = pos + 1U;
        }
        if (f.size() != 4U || f[1].empty() || f[1].size() > 400U) return fail("skill-learner record malformed");
        try {
            if (f[0] == "B") {
                const double a = std::stod(f[2]);
                const double b = std::stod(f[3]);
                if (!std::isfinite(a) || !std::isfinite(b) || a < 0.0 || b < 0.0 || a > 1e9 || b > 1e9)
                    return fail("skill-learner record out of bounds");
                beta[f[1]] = Beta{a, b};
            } else if (f[0] == "F") {
                FailState s;
                s.last_fail_tick = std::stoull(f[2]);
                s.streak = std::stoi(f[3]);
                if (s.streak < 0 || s.streak > 1000000) return fail("skill-learner record out of bounds");
                fails[f[1]] = s;
            } else {
                return fail("skill-learner record type unknown");
            }
        } catch (...) {
            return fail("skill-learner record unparsable");
        }
        if (beta.size() + fails.size() > cfg_.max_entries) return fail("skill-learner file exceeds entry limit");
    }
    beta_ = std::move(beta);
    fails_ = std::move(fails);
    return true;
}

} // namespace bedrockai
