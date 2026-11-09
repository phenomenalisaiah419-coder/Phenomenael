#include "core/opponent_model.hpp"

#include <algorithm>
#include <cmath>

namespace bedrockai {

const char* archetype_name(Archetype a) {
    switch (a) {
        case Archetype::Unknown: return "unknown";
        case Archetype::Rusher: return "rusher";
        case Archetype::Strafer: return "strafer";
        case Archetype::Defensive: return "defensive";
        case Archetype::Evasive: return "evasive";
        case Archetype::Ranged: return "ranged";
    }
    return "unknown";
}

OpponentModel::OpponentModel(OpponentModelConfig config) : cfg_(config) {}

void OpponentModel::observe(const std::string& id, std::uint64_t tick, const Vec3& opponent, const Vec3& player,
                            bool used_ranged) {
    if (id.empty() || !std::isfinite(opponent.x + opponent.y + opponent.z + player.x + player.y + player.z)) return;
    auto it = tracks_.find(id);
    if (it == tracks_.end()) {
        if (tracks_.size() >= cfg_.max_opponents) {
            auto oldest = tracks_.begin();
            for (auto i = tracks_.begin(); i != tracks_.end(); ++i)
                if (i->second.last_seen < oldest->second.last_seen) oldest = i;
            tracks_.erase(oldest);
        }
        it = tracks_.emplace(id, Track{}).first;
    }
    Track& t = it->second;
    if (!t.samples.empty() && tick <= t.samples.back().tick) return;  // stale / duplicate sample
    t.samples.push_back(Sample{tick, opponent, player});
    while (t.samples.size() > cfg_.history) t.samples.pop_front();
    ++t.total;
    if (used_ranged) ++t.ranged;
    t.last_seen = tick;
}

OpponentModel::Stats OpponentModel::analyse(const Track& t) const {
    Stats s;
    const std::size_t n = t.samples.size();
    if (n < 4U) return s;
    double sum_speed = 0.0, sum_radial = 0.0, sum_lat_abs = 0.0;
    double interval_sum = 0.0;
    int intervals = 0;
    int count = 0;
    int last_sign = 0;
    std::uint64_t prev_flip = 0;
    bool have_prev_flip = false;
    std::vector<double> recent_r, recent_l;
    for (std::size_t i = 1; i < n; ++i) {
        const Sample& a = t.samples[i - 1U];
        const Sample& b = t.samples[i];
        const double dt = static_cast<double>(std::max<std::uint64_t>(1U, b.tick - a.tick));
        const double dx = b.opp.x - a.opp.x;
        const double dz = b.opp.z - a.opp.z;
        double nx = a.me.x - a.opp.x;
        double nz = a.me.z - a.opp.z;
        const double len = std::hypot(nx, nz);
        if (len < 1e-6) continue;
        nx /= len;
        nz /= len;
        const double radial = (dx * nx + dz * nz) / dt;
        const double lateral = (nx * dz - nz * dx) / dt;
        sum_speed += std::hypot(dx, dz) / dt;
        sum_radial += radial;
        sum_lat_abs += std::fabs(lateral);
        ++count;
        recent_r.push_back(radial);
        recent_l.push_back(lateral);
        const int sign = lateral > cfg_.lateral_deadband ? 1 : (lateral < -cfg_.lateral_deadband ? -1 : 0);
        if (sign != 0) {
            if (last_sign != 0 && sign != last_sign) {
                ++s.flips;
                s.last_flip_tick = b.tick;
                s.has_flip = true;
                if (have_prev_flip) {
                    interval_sum += static_cast<double>(b.tick - prev_flip);
                    ++intervals;
                }
                prev_flip = b.tick;
                have_prev_flip = true;
            }
            last_sign = sign;
        }
    }
    if (count == 0) return s;
    const double c = static_cast<double>(count);
    s.speed = sum_speed / c;
    s.radial = sum_radial / c;
    s.lateral_abs = sum_lat_abs / c;
    s.period = intervals > 0 ? interval_sum / static_cast<double>(intervals) : 0.0;
    const std::size_t k = std::min<std::size_t>(3U, recent_r.size());
    for (std::size_t i = recent_r.size() - k; i < recent_r.size(); ++i) {
        s.v_radial += recent_r[i] / static_cast<double>(k);
        s.v_lateral += recent_l[i] / static_cast<double>(k);
    }
    const Sample& last = t.samples.back();
    double nx = last.me.x - last.opp.x;
    double nz = last.me.z - last.opp.z;
    const double len = std::hypot(nx, nz);
    if (len > 1e-6) {
        s.nx = nx / len;
        s.nz = nz / len;
    }
    s.ok = true;
    return s;
}

OpponentProfile OpponentModel::profile(const std::string& id) const {
    OpponentProfile p;
    p.id = id;
    const auto it = tracks_.find(id);
    if (it == tracks_.end()) return p;
    const Track& t = it->second;
    p.samples = static_cast<int>(t.samples.size());
    p.ranged_fraction = t.total ? static_cast<double>(t.ranged) / static_cast<double>(t.total) : 0.0;
    const Stats s = analyse(t);
    if (!s.ok) return p;
    p.mean_speed = s.speed;
    p.mean_radial = s.radial;
    p.mean_lateral = s.lateral_abs;
    p.strafe_period_ticks = (s.flips >= 2) ? s.period : 0.0;
    const double maturity = std::min(1.0, static_cast<double>(p.samples) / static_cast<double>(cfg_.min_samples));
    if (p.samples < cfg_.min_samples) {
        p.archetype = Archetype::Unknown;
        p.confidence = 0.3 * maturity;
        return p;
    }
    if (p.ranged_fraction > 0.3) {
        p.archetype = Archetype::Ranged;
    } else if (s.speed < 0.03) {
        p.archetype = Archetype::Defensive;
    } else if (s.flips >= 2 && s.lateral_abs > 0.04 && s.lateral_abs > 0.5 * std::fabs(s.radial)) {
        p.archetype = Archetype::Strafer;
    } else if (s.radial > 0.05) {
        p.archetype = Archetype::Rusher;
    } else if (s.radial < -0.05) {
        p.archetype = Archetype::Evasive;
    } else {
        p.archetype = Archetype::Defensive;
    }
    p.confidence = 0.55 + 0.4 * maturity * (p.archetype == Archetype::Strafer ? std::min(1.0, static_cast<double>(s.flips) / 4.0) : 1.0);
    p.confidence = std::min(0.95, p.confidence);
    return p;
}

Vec3 OpponentModel::predict_linear(const std::string& id, int ticks_ahead) const {
    const auto it = tracks_.find(id);
    if (it == tracks_.end() || it->second.samples.empty()) return Vec3{};
    const auto& s = it->second.samples;
    const Vec3 last = s.back().opp;
    if (s.size() < 2U) return last;
    const Sample& prev = s[s.size() - 2U];
    const double dt = static_cast<double>(std::max<std::uint64_t>(1U, s.back().tick - prev.tick));
    const double k = static_cast<double>(std::max(0, ticks_ahead));
    return Vec3{last.x + (last.x - prev.opp.x) / dt * k, last.y, last.z + (last.z - prev.opp.z) / dt * k};
}

Vec3 OpponentModel::predict_position(const std::string& id, int ticks_ahead) const {
    const auto it = tracks_.find(id);
    if (it == tracks_.end() || it->second.samples.empty()) return Vec3{};
    const Track& t = it->second;
    const Stats s = analyse(t);
    const OpponentProfile prof = profile(id);
    const Vec3 last = t.samples.back().opp;
    const int n = std::max(0, std::min(ticks_ahead, 200));
    if (!s.ok || prof.archetype != Archetype::Strafer || prof.strafe_period_ticks < 2.0) return predict_linear(id, n);
    // Strafer: keep the radial motion, replay the lateral square wave with the learned period.
    const double perp_x = -s.nz;
    const double perp_z = s.nx;
    double v_lat = (s.v_lateral >= 0.0 ? 1.0 : -1.0) * s.lateral_abs;
    double since_flip = s.has_flip ? static_cast<double>(t.samples.back().tick - s.last_flip_tick) : 0.0;
    double x = last.x;
    double z = last.z;
    for (int i = 0; i < n; ++i) {
        since_flip += 1.0;
        if (since_flip >= prof.strafe_period_ticks) {
            v_lat = -v_lat;
            since_flip = 0.0;
        }
        x += s.v_radial * s.nx + v_lat * perp_x;
        z += s.v_radial * s.nz + v_lat * perp_z;
    }
    return Vec3{x, last.y, z};
}

} // namespace bedrockai
