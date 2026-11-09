#include "core/hazard_scan.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bedrockai {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNegInf = -std::numeric_limits<double>::infinity();

int fl(double v) { return static_cast<int>(std::floor(v)); }

struct Surface {
    bool found = false;
    double top = kNegInf;
    BlockClass cls = BlockClass::Unknown;
};

bool body_blocked(const WorldGrid& g, double x, double y, double z, double hw) {
    const double offsets[2] = {-hw, hw};
    for (int dy = 0; dy < 2; ++dy) {
        const int cy = fl(y + 0.01) + dy;
        for (const double ox : offsets)
            for (const double oz : offsets)
                if (block_is_support(g.get(fl(x + ox), cy, fl(z + oz)))) return true;
    }
    return false;
}

// Highest landing surface (support, liquid, web, ladder) whose top lies in (to_y, from_y].
Surface surface_between(const WorldGrid& g, double x, double z, double from_y, double to_y, double hw) {
    Surface best;
    const double offsets[2] = {-hw, hw};
    for (const double ox : offsets) {
        for (const double oz : offsets) {
            const int cx = fl(x + ox);
            const int cz = fl(z + oz);
            for (int cy = fl(from_y - 1e-9); cy >= fl(to_y - 1e-9); --cy) {
                const BlockClass c = g.get(cx, cy, cz);
                const bool lands = block_is_support(c) || block_is_liquid(c) || c == BlockClass::Web ||
                                   c == BlockClass::Climbable;
                if (!lands) continue;
                const double top = static_cast<double>(cy) + 1.0;
                if (top <= from_y + 1e-6) {
                    if (!best.found || top > best.top) best = Surface{true, top, c};
                    break;
                }
            }
        }
    }
    return best;
}

} // namespace

MotionPredictor::MotionPredictor(KinematicsConfig config) : config_(config) {}

std::vector<PathPoint> MotionPredictor::predict(const WorldGrid& g, const Vec3& feet, double vertical_velocity,
                                                bool on_ground, double yaw_deg, double stick_forward,
                                                double stick_strafe, bool sprint, bool sneak, bool jump,
                                                int ticks) const {
    std::vector<PathPoint> out;
    const int n = std::max(0, std::min(ticks, 400));
    out.reserve(static_cast<std::size_t>(n) + 1U);
    Vec3 p = feet;
    double vy = vertical_velocity;
    bool grounded = on_ground;
    const double hw = config_.half_width;
    const double yaw = yaw_deg * kPi / 180.0;
    double dx = stick_forward * -std::sin(yaw) + stick_strafe * std::cos(yaw);
    double dz = stick_forward * std::cos(yaw) + stick_strafe * std::sin(yaw);
    const double mag = std::hypot(dx, dz);
    if (mag > 1.0) {
        dx /= mag;
        dz /= mag;
    }
    // `sprint` is the EFFECTIVE sprint flag (callers apply the "sprint needs forward input" rule in
    // the camera frame, which this world-aligned predictor cannot see).
    const double speed = sneak ? config_.sneak_per_tick : (sprint ? config_.sprint_per_tick : config_.walk_per_tick);
    const double sx = dx * speed;
    const double sz = dz * speed;

    out.push_back(PathPoint{p, !grounded, vy, BlockClass::Unknown});
    for (int t = 1; t <= n; ++t) {
        const bool was_grounded = grounded;
        // Horizontal movement, axis by axis, with body collision and sneak edge protection.
        const double nx = p.x + sx;
        if (!body_blocked(g, nx, p.y, p.z, hw)) {
            const bool edge_ok = !(sneak && was_grounded) ||
                                 surface_between(g, nx, p.z, p.y + 0.05, p.y - 0.6, hw).found;
            if (edge_ok) p.x = nx;
        }
        const double nz = p.z + sz;
        if (!body_blocked(g, p.x, p.y, nz, hw)) {
            const bool edge_ok = !(sneak && was_grounded) ||
                                 surface_between(g, p.x, nz, p.y + 0.05, p.y - 0.6, hw).found;
            if (edge_ok) p.z = nz;
        }
        BlockClass landed = BlockClass::Unknown;
        if (grounded) {
            if (jump) {
                vy = config_.jump_velocity;
                grounded = false;
            } else if (!surface_between(g, p.x, p.z, p.y + 0.05, p.y - 0.05, hw).found) {
                grounded = false;
                vy = 0.0;
            }
        }
        if (!grounded) {
            const double ny = p.y + vy;
            if (vy <= 0.0) {
                const Surface s = surface_between(g, p.x, p.z, p.y, ny, hw);
                if (s.found && s.top >= ny - 1e-9) {
                    p.y = s.top;
                    grounded = true;
                    vy = 0.0;
                    landed = s.cls;
                } else {
                    p.y = ny;
                    vy = (vy - config_.gravity_per_tick) * config_.air_drag;
                }
            } else {
                p.y = ny;
                vy = (vy - config_.gravity_per_tick) * config_.air_drag;
            }
        }
        out.push_back(PathPoint{p, !grounded, vy, landed});
    }
    return out;
}

HazardScanner::HazardScanner(HazardConfig config) : config_(config) {}

double HazardScanner::fall_damage(double blocks) const {
    if (!std::isfinite(blocks)) return 0.0;
    return std::max(0.0, std::ceil(blocks - config_.max_free_fall));
}

ColumnProbe HazardScanner::probe_column(const WorldGrid& g, int cx, int cz, int start_y) const {
    for (int y = start_y; y >= g.min_y(); --y) {
        const BlockClass c = g.get(cx, y, cz);
        if (block_is_support(c)) return ColumnProbe{ColumnProbe::Kind::Support, y, c};
        if (block_is_liquid(c)) return ColumnProbe{ColumnProbe::Kind::Liquid, y, c};
        if (c == BlockClass::Web || c == BlockClass::Climbable) return ColumnProbe{ColumnProbe::Kind::Soft, y, c};
        if (c == BlockClass::Unknown) return ColumnProbe{ColumnProbe::Kind::Unknown, y, c};
    }
    return ColumnProbe{ColumnProbe::Kind::Void, g.min_y() - 1, BlockClass::Air};
}

ColumnProbe HazardScanner::probe_below(const WorldGrid& g, const Vec3& feet) const {
    const double hw = config_.half_width;
    const double offsets[2] = {-hw, hw};
    const int start_y = fl(feet.y - 1e-9);
    ColumnProbe best;
    bool have_landing = false;
    bool any_unknown = false;
    for (const double ox : offsets) {
        for (const double oz : offsets) {
            const ColumnProbe c = probe_column(g, fl(feet.x + ox), fl(feet.z + oz), start_y);
            if (c.kind == ColumnProbe::Kind::Unknown) any_unknown = true;
            if (c.kind == ColumnProbe::Kind::Support || c.kind == ColumnProbe::Kind::Liquid ||
                c.kind == ColumnProbe::Kind::Soft) {
                if (!have_landing || c.cell_y > best.cell_y) best = c;
                have_landing = true;
            }
        }
    }
    if (have_landing) return best;
    if (any_unknown) return ColumnProbe{ColumnProbe::Kind::Unknown, start_y, BlockClass::Unknown};
    return ColumnProbe{ColumnProbe::Kind::Void, g.min_y() - 1, BlockClass::Air};
}

HazardReport HazardScanner::scan_path(const WorldGrid& g, const std::vector<PathPoint>& path, double health,
                                      double already_fallen) const {
    HazardReport r;
    int run_start = -1;
    double run_top_y = 0.0;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const PathPoint& pt = path[i];
        const int idx = static_cast<int>(i);
        if (g.get(fl(pt.pos.x), fl(pt.pos.y + 0.01), fl(pt.pos.z)) == BlockClass::Lava) {
            r = HazardReport{};
            r.found = true;
            r.kind = HazardKind::Lava;
            r.tick = idx;
            r.impact_tick = idx;
            r.position = pt.pos;
            r.lethal = true;
            r.landing = BlockClass::Lava;
            return r;
        }
        if (!pt.airborne) {
            if (run_start >= 0) {
                // Landing inside the horizon: evaluate the completed fall.
                if (pt.landed == BlockClass::Lava) {
                    r = HazardReport{};
                    r.found = true;
                    r.kind = HazardKind::Lava;
                    r.tick = run_start;
                    r.impact_tick = idx;
                    r.position = path[static_cast<std::size_t>(run_start)].pos;
                    r.fall_blocks = run_top_y - pt.pos.y + already_fallen;
                    r.lethal = true;
                    r.landing = BlockClass::Lava;
                    return r;
                }
                const double fall = run_top_y - pt.pos.y + already_fallen;
                const double dmg = pt.landed == BlockClass::Bounce ? 0.0
                                   : (block_is_liquid(pt.landed) || pt.landed == BlockClass::Web ||
                                      pt.landed == BlockClass::Climbable) ? 0.0 : fall_damage(fall);
                if (dmg > 0.0) {
                    r.found = true;
                    r.kind = dmg >= health ? HazardKind::LethalFall : HazardKind::DamagingFall;
                    r.tick = run_start;
                    r.impact_tick = idx;
                    r.position = path[static_cast<std::size_t>(run_start)].pos;
                    r.fall_blocks = fall;
                    r.expected_damage = dmg;
                    r.lethal = dmg >= health;
                    r.landing = pt.landed;
                    r.landing_y = fl(pt.pos.y - 1e-6);
                    return r;
                }
                run_start = -1;
            }
            const BlockClass under = g.get(fl(pt.pos.x), fl(pt.pos.y - 0.01), fl(pt.pos.z));
            if (under == BlockClass::Hazard) {
                r.found = true;
                r.kind = HazardKind::HarmfulBlock;
                r.tick = idx;
                r.impact_tick = idx;
                r.position = pt.pos;
                r.expected_damage = config_.harmful_block_damage;
                r.landing = BlockClass::Hazard;
                return r;
            }
            continue;
        }
        if (run_start < 0) {
            run_start = idx;
            run_top_y = pt.pos.y;
        }
        run_top_y = std::max(run_top_y, pt.pos.y);
    }
    if (run_start >= 0) {
        // Still airborne at the horizon: the landing lies beyond the predicted path.
        const PathPoint& last = path.back();
        const ColumnProbe probe = probe_below(g, last.pos);
        r.found = true;
        r.tick = run_start;
        r.impact_tick = -1;
        r.position = path[static_cast<std::size_t>(run_start)].pos;
        r.landing = probe.cls;
        r.landing_y = probe.cell_y;
        switch (probe.kind) {
            case ColumnProbe::Kind::Void:
                r.kind = HazardKind::Void;
                r.lethal = true;
                r.fall_blocks = run_top_y - static_cast<double>(g.min_y()) + already_fallen;
                r.expected_damage = health;
                return r;
            case ColumnProbe::Kind::Unknown:
                r.kind = HazardKind::UnknownDrop;
                r.certain = false;
                return r;
            case ColumnProbe::Kind::Liquid:
                if (probe.cls == BlockClass::Lava) {
                    r.kind = HazardKind::Lava;
                    r.lethal = true;
                    return r;
                }
                return HazardReport{};
            case ColumnProbe::Kind::Soft:
                return HazardReport{};
            case ColumnProbe::Kind::Support: {
                const double top = static_cast<double>(probe.cell_y) + 1.0;
                const double fall = run_top_y - top + already_fallen;
                const double dmg = probe.cls == BlockClass::Bounce ? 0.0 : fall_damage(fall);
                if (dmg <= 0.0) return HazardReport{};
                r.kind = dmg >= health ? HazardKind::LethalFall : HazardKind::DamagingFall;
                r.fall_blocks = fall;
                r.expected_damage = dmg;
                r.lethal = dmg >= health;
                return r;
            }
        }
    }
    return HazardReport{};
}

} // namespace bedrockai
