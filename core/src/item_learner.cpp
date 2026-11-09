#include "core/item_learner.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace bedrockai {
namespace {

double vlen(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
bool finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return (n % 2U == 1U) ? v[n / 2U] : 0.5 * (v[n / 2U - 1U] + v[n / 2U]);
}

ItemCapabilityMask bit(ItemCapability c) { return capability_mask(c); }

ItemCapabilityMask infer_caps(const ItemTrial& t, const ItemLearnerConfig& cfg) {
    ItemCapabilityMask m = 0U;
    const double vel = vlen(t.self_velocity_delta);
    if (vel >= cfg.velocity_threshold) {
        m |= bit(ItemCapability::BurstMovement) | bit(ItemCapability::Mobility);
        if (t.self_velocity_delta.y >= cfg.velocity_threshold) m |= bit(ItemCapability::ClutchRecovery);
    }
    if (t.self_health_delta >= cfg.heal_threshold) {
        m |= bit(ItemCapability::Healing);
        if (t.activation == ItemActivationMode::Consume) m |= bit(ItemCapability::Drinkable);
    }
    if (t.hunger_delta >= 1.0) m |= bit(ItemCapability::Food);
    if (t.projectile_spawned) {
        m |= bit(ItemCapability::Projectile);
        if (t.activation == ItemActivationMode::Throw) m |= bit(ItemCapability::Throwable);
        if (t.activation == ItemActivationMode::Use && t.target_damage >= cfg.damage_threshold)
            m |= bit(ItemCapability::RangedWeapon);
    }
    if (t.target_damage >= cfg.damage_threshold && t.activation == ItemActivationMode::Attack)
        m |= bit(ItemCapability::MeleeWeapon);
    if (t.target_knockback >= cfg.knockback_threshold) m |= bit(ItemCapability::Knockback);
    if (t.teleported && vlen(t.teleport_delta) >= cfg.teleport_threshold)
        m |= bit(ItemCapability::Teleport) | bit(ItemCapability::Mobility);
    if (t.blocks_placed) m |= bit(ItemCapability::BlockPlace);
    return m;
}

} // namespace

ItemLearner::ItemLearner(ItemLearnerConfig config) : cfg_(config) {}

std::optional<ProjectilePhysics> ItemLearner::fit_projectile(const std::vector<TimedPosition>& s) const {
    if (s.size() < 5U) return std::nullopt;
    std::vector<Vec3> v;
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (!finite3(s[i].position) || !finite3(s[i - 1U].position) || s[i].tick <= s[i - 1U].tick) return std::nullopt;
        const double dt = static_cast<double>(s[i].tick - s[i - 1U].tick);
        v.push_back(Vec3{(s[i].position.x - s[i - 1U].position.x) / dt, (s[i].position.y - s[i - 1U].position.y) / dt,
                         (s[i].position.z - s[i - 1U].position.z) / dt});
    }
    std::vector<double> drags;
    for (std::size_t k = 0; k + 1U < v.size(); ++k) {
        const double h0 = std::hypot(v[k].x, v[k].z);
        const double h1 = std::hypot(v[k + 1U].x, v[k + 1U].z);
        if (h0 > 0.05) drags.push_back(h1 / h0);
    }
    if (drags.size() < 2U) return std::nullopt;
    const double drag = median(drags);
    if (!(drag > 0.5 && drag <= 1.0)) return std::nullopt;
    double g_sum = 0.0;
    for (std::size_t k = 0; k + 1U < v.size(); ++k) g_sum += v[k].y * drag - v[k + 1U].y;
    const double g_tick = g_sum / static_cast<double>(v.size() - 1U);
    ProjectilePhysics p;
    p.launch_speed = vlen(v.front()) * cfg_.tick_rate;
    p.gravity = g_tick * cfg_.tick_rate * cfg_.tick_rate;
    p.drag_per_tick = drag;
    p.tick_rate = cfg_.tick_rate;
    p.calibrated = false;
    if (!(p.launch_speed > 0.0) || !std::isfinite(p.gravity)) return std::nullopt;
    return p;
}

std::optional<ProjectilePhysics> ItemLearner::consensus(const std::vector<ProjectilePhysics>& fits) const {
    if (fits.empty()) return std::nullopt;
    std::vector<double> sp, gr, dr;
    for (const auto& f : fits) {
        sp.push_back(f.launch_speed);
        gr.push_back(f.gravity);
        dr.push_back(f.drag_per_tick);
    }
    ProjectilePhysics out = fits.front();
    out.launch_speed = median(sp);
    out.gravity = median(gr);
    out.drag_per_tick = median(dr);
    int consistent = 0;
    for (const auto& f : fits) {
        const bool s_ok = std::fabs(f.launch_speed - out.launch_speed) <= 0.10 * std::max(out.launch_speed, 1e-6);
        const bool g_ok = std::fabs(f.gravity - out.gravity) <= 0.15 * std::max(std::fabs(out.gravity), 0.5);
        const bool d_ok = std::fabs(f.drag_per_tick - out.drag_per_tick) <= 0.01;
        if (s_ok && g_ok && d_ok) ++consistent;
    }
    out.calibrated = consistent >= 3;  // the brain refuses uncalibrated ranged physics
    return out;
}

void ItemLearner::observe(const ItemTrial& t) {
    if (t.item_key.empty() || !finite3(t.self_velocity_delta) || !finite3(t.teleport_delta) ||
        !std::isfinite(t.self_health_delta) || !std::isfinite(t.hunger_delta) || !std::isfinite(t.target_damage) ||
        !std::isfinite(t.target_knockback))
        return;
    auto it = items_.find(t.item_key);
    if (it == items_.end()) {
        if (items_.size() >= cfg_.max_items) return;
        it = items_.emplace(t.item_key, Record{}).first;
    }
    Record& r = it->second;
    ++r.trials;
    if (!t.by_human) ++r.ai_trials;
    r.activations |= activation_mask(t.activation);
    r.health_sum += t.self_health_delta;
    const ItemCapabilityMask m = infer_caps(t, cfg_);
    for (std::uint64_t b = 1U; b != 0U && b <= capability_mask(ItemCapability::UnsafeConsumable); b <<= 1U)
        if ((m & b) != 0U) ++r.hits[b];
    if (t.activation == ItemActivationMode::Consume && t.self_health_delta <= -cfg_.hurt_threshold) r.unsafe = true;
    if (t.projectile_spawned) {
        const auto fit = fit_projectile(t.projectile_samples);
        if (fit.has_value()) {
            r.fits.push_back(*fit);
            if (r.fits.size() > 12U) r.fits.erase(r.fits.begin());
        }
    }
}

LearnedItem ItemLearner::learned(const std::string& key) const {
    LearnedItem out;
    out.item_key = key;
    const auto it = items_.find(key);
    if (it == items_.end()) return out;
    const Record& r = it->second;
    out.trials = r.trials;
    out.unsafe = r.unsafe;
    out.activations = r.activations;
    out.mean_self_health_delta = r.trials ? r.health_sum / static_cast<double>(r.trials) : 0.0;
    double conf_sum = 0.0;
    int learned_bits = 0;
    for (const auto& kv : r.hits) {
        const double conf = (static_cast<double>(kv.second) + 1.0) / (static_cast<double>(r.trials) + 2.0);
        if (r.trials >= cfg_.min_trials && kv.second >= 2U && conf >= cfg_.min_confidence) {
            out.capabilities |= kv.first;
            conf_sum += conf;
            ++learned_bits;
        }
    }
    out.confidence = learned_bits > 0 ? conf_sum / static_cast<double>(learned_bits) : 0.0;
    if (has_capability(out.capabilities, ItemCapability::Projectile) ||
        has_capability(out.capabilities, ItemCapability::Throwable))
        out.projectile = consensus(r.fits);
    std::ostringstream e;
    e << "learned from " << r.trials << " observed use(s), " << r.ai_trials << " by AI trial";
    out.evidence = e.str();
    return out;
}

bool ItemLearner::usable_for(const std::string& key, ItemCapability capability) const {
    const LearnedItem l = learned(key);
    if (l.unsafe) return false;
    if (!has_capability(l.capabilities, capability)) return false;
    if (capability == ItemCapability::Projectile || capability == ItemCapability::Throwable ||
        capability == ItemCapability::RangedWeapon)
        return l.projectile.has_value() && l.projectile->calibrated;
    return true;
}

ExperimentVerdict ItemLearner::experiment_verdict(const std::string& key, const ExperimentContext& c) const {
    auto deny = [](const char* why) { return ExperimentVerdict{false, why}; };
    if (!c.user_allows_experiments) return deny("item experiments are not enabled by the user");
    if (!c.automation_permitted) return deny("automation is not permitted on this server");
    if (c.emergency) return deny("no experiments during an emergency");
    if (c.enemies_nearby) return deny("enemies nearby");
    if (!c.footing_safe) return deny("footing is not safe");
    if (c.health_fraction < 0.8) return deny("health too low to experiment");
    if (c.activation == ItemActivationMode::Consume && !c.user_allows_consumables)
        return deny("consuming an unknown item needs separate user consent");
    if (c.activation == ItemActivationMode::Attack) return deny("attack trials need a controlled target");
    const auto it = items_.find(key);
    if (it != items_.end()) {
        if (it->second.unsafe) return deny("item previously hurt the user");
        if (it->second.ai_trials >= cfg_.max_ai_trials_per_item) return deny("trial budget exhausted");
    }
    return ExperimentVerdict{true, "experiment allowed"};
}

bool ItemLearner::promote(const std::string& key, const ItemDescriptor& d, ItemKnowledgeBase& base,
                          std::string* error) const {
    auto fail = [&](const char* m) {
        if (error) *error = m;
        return false;
    };
    const LearnedItem l = learned(key);
    if (l.unsafe) return fail("item is flagged unsafe");
    if (l.capabilities == 0U || l.trials < cfg_.min_trials) return fail("not enough evidence");
    if (d.identifier.empty()) return fail("descriptor has no identifier");
    ItemKnowledge k;
    k.identifier = d.identifier;
    k.display_name = d.display_name;
    k.capabilities = l.capabilities;
    k.activations = l.activations;
    for (unsigned m = 1U; m <= static_cast<unsigned>(ItemActivationMode::Interact); ++m) {
        if (has_activation(l.activations, static_cast<ItemActivationMode>(m))) {
            k.activation = static_cast<ItemActivationMode>(m);
            break;
        }
    }
    k.verified = true;
    k.confidence = l.confidence;
    k.observed_outcomes = l.trials;
    k.evidence = l.evidence;
    k.variant_key = d.variant_key;
    if (l.projectile.has_value() && l.projectile->calibrated) k.projectile_physics = l.projectile;
    return base.register_profile(k, error);
}

} // namespace bedrockai
