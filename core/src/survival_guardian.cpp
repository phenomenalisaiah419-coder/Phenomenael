#include "core/survival_guardian.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bedrockai {
namespace {

constexpr double kPi = 3.14159265358979323846;

int fl(double v) { return static_cast<int>(std::floor(v)); }
double dist3(const Vec3& a, const Vec3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
bool finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct Support {
    bool ok = false;
    PlacementOrder order{};
};

// The straight segment eye->target must not pass through any solid block other than the support block.
bool ray_clear(const WorldGrid& g, const Vec3& eye, const Vec3& target, int ax, int ay, int az) {
    const double dx = target.x - eye.x;
    const double dy = target.y - eye.y;
    const double dz = target.z - eye.z;
    const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-9) return true;
    const int steps = static_cast<int>(std::ceil(len / 0.1));
    for (int i = 1; i < steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const int cx = fl(eye.x + dx * t);
        const int cy = fl(eye.y + dy * t);
        const int cz = fl(eye.z + dz * t);
        if (cx == ax && cy == ay && cz == az) continue;
        if (block_is_support(g.get(cx, cy, cz))) return false;
    }
    return true;
}

// Finds a block whose face can be clicked from `eye` to fill the (empty) target cell.
// Only plain solid blocks are ever clicked: a bed would start sleeping, protected blocks are off limits.
Support find_support(const WorldGrid& g, int cx, int cy, int cz, const Vec3& eye, double reach) {
    static const int kDir[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}};
    Support best;
    double best_d = std::numeric_limits<double>::infinity();
    for (const auto& d : kDir) {
        const int sx = cx + d[0];
        const int sy = cy + d[1];
        const int sz = cz + d[2];
        const BlockClass cls = g.get(sx, sy, sz);
        if (cls != BlockClass::Solid && cls != BlockClass::Bounce) continue;
        const int fx = -d[0];
        const int fy = -d[1];
        const int fz = -d[2];
        const Vec3 click{static_cast<double>(sx) + 0.5 + 0.5 * fx, static_cast<double>(sy) + 0.5 + 0.5 * fy,
                         static_cast<double>(sz) + 0.5 + 0.5 * fz};
        const double dd = dist3(eye, click);
        if (dd > reach || dd >= best_d) continue;
        if (!ray_clear(g, eye, click, sx, sy, sz)) continue;
        best.ok = true;
        best_d = dd;
        best.order.x = cx;
        best.order.y = cy;
        best.order.z = cz;
        best.order.support_x = sx;
        best.order.support_y = sy;
        best.order.support_z = sz;
        best.order.face = fy < 0 ? 0 : (fy > 0 ? 1 : (fz < 0 ? 2 : (fz > 0 ? 3 : (fx < 0 ? 4 : 5))));
        best.order.click_point = click;
    }
    return best;
}

bool standable(const WorldGrid& g, int x, int y, int z) {
    if (g.get(x, y - 1, z) != BlockClass::Solid) return false;
    if (g.get(x, y, z) != BlockClass::Air || g.get(x, y + 1, z) != BlockClass::Air) return false;
    int support_neighbours = 0;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue;
            const BlockClass under = g.get(x + dx, y - 1, z + dz);
            if (under == BlockClass::Lava || under == BlockClass::Hazard) return false;
            if (block_is_support(under)) ++support_neighbours;
        }
    return support_neighbours >= 3;  // a platform, not a pillar tip
}

} // namespace

const char* guardian_action_name(GuardianAction a) {
    switch (a) {
        case GuardianAction::None: return "none";
        case GuardianAction::Halt: return "halt";
        case GuardianAction::StepBack: return "step_back";
        case GuardianAction::BridgeForward: return "bridge_forward";
        case GuardianAction::BlockClutch: return "block_clutch";
        case GuardianAction::PearlEscape: return "pearl_escape";
        case GuardianAction::WaterClutch: return "water_clutch";
    }
    return "none";
}

bool find_placement(const WorldGrid& grid, int x, int y, int z, const Vec3& eye, double reach, PlacementOrder* out) {
    if (grid.get(x, y, z) != BlockClass::Air) return false;
    const Support s = find_support(grid, x, y, z, eye, reach);
    if (!s.ok) return false;
    if (out) *out = s.order;
    return true;
}

void aim_at(const Vec3& eye, const Vec3& target, double* yaw_deg, double* pitch_deg) {
    const double dx = target.x - eye.x;
    const double dy = target.y - eye.y;
    const double dz = target.z - eye.z;
    *yaw_deg = std::atan2(-dx, dz) * 180.0 / kPi;
    *pitch_deg = -std::atan2(dy, std::hypot(dx, dz)) * 180.0 / kPi;
}

SurvivalGuardian::SurvivalGuardian(GuardianConfig config, KinematicsConfig kinematics, HazardConfig hazards)
    : cfg_(config), motion_(kinematics), scanner_(hazards) {}

std::vector<PathPoint> SurvivalGuardian::predict_path(const GuardianInput& in) const {
    if (!in.grid) return {};
    return motion_.predict(*in.grid, in.feet, in.vertical_velocity, in.on_ground, in.yaw_deg, in.stick_forward,
                           in.stick_strafe, in.sprint, in.sneak, in.jump, cfg_.horizon_ticks);
}

GuardianPlan SurvivalGuardian::evaluate(const GuardianInput& in) const {
    GuardianPlan plan;
    if (!in.grid || !finite3(in.feet) || !std::isfinite(in.health) || !std::isfinite(in.yaw_deg) ||
        !std::isfinite(in.stick_forward) || !std::isfinite(in.stick_strafe) || !std::isfinite(in.vertical_velocity)) {
        plan.reason = "no usable world/player data";
        return plan;
    }
    const WorldGrid& g = *in.grid;
    const std::vector<PathPoint> path = predict_path(in);
    plan.hazard = scanner_.scan_path(g, path, in.health, in.already_fallen);
    const HazardReport& h = plan.hazard;
    auto is_danger = [&](const HazardReport& r) {
        return r.found && (r.lethal || r.kind == HazardKind::Lava ||
                           (r.kind == HazardKind::DamagingFall && r.expected_damage >= cfg_.min_damage_to_act) ||
                           (r.kind == HazardKind::UnknownDrop && cfg_.unknown_drop_is_danger));
    };
    const bool danger = is_danger(h);
    if (!danger) {
        plan.reason = "predicted path is safe";
        return plan;
    }
    const std::size_t hz = h.tick >= 0 ? static_cast<std::size_t>(h.tick) : 0U;
    const int t_leave = std::max(0, h.tick);
    const bool airborne_now = !in.on_ground;
    // A held jump means ground contact (the only moment a halt is possible) comes once per hop:
    // intervene before take-off whenever the whole horizon is dangerous.
    const int lead = in.jump ? cfg_.horizon_ticks : cfg_.lead_ticks;
    if (!airborne_now && t_leave > lead) {
        // Minimal intrusion: the human keeps full control until intervention is actually needed.
        plan.warning = true;
        plan.reason = "hazard predicted in " + std::to_string(t_leave) + " ticks: warning only";
        return plan;
    }
    plan.intervene = true;
    const bool fall_hazard = h.kind == HazardKind::Void || h.kind == HazardKind::LethalFall ||
                             h.kind == HazardKind::DamagingFall || h.kind == HazardKind::UnknownDrop;
    const bool ledge = fall_hazard && !airborne_now && hz >= 1U && hz < path.size() && path[hz].airborne &&
                       !path[hz - 1U].airborne;
    const Vec3 eye{in.feet.x, in.feet.y + cfg_.eye_height, in.feet.z};
    auto eff = [&](const std::string& id) {
        const double e = in.effectiveness ? in.effectiveness(id) : 1.0;
        return std::max(0.05, std::min(1.0, std::isfinite(e) ? e : 1.0));
    };
    auto describe = [&]() {
        switch (h.kind) {
            case HazardKind::Void: return "void";
            case HazardKind::Lava: return "lava";
            case HazardKind::LethalFall: return "lethal fall";
            case HazardKind::DamagingFall: return "damaging fall";
            case HazardKind::UnknownDrop: return "unobserved drop";
            default: return "hazard";
        }
    };

    // Mid-air rescues suppress the human's horizontal input (the AI can only aim a clutch at a column
    // the player is actually falling through), so they are analysed on the zero-input path.  Mid-air
    // steering itself has limited authority in the real game; the model treats it as full control, so
    // it is scored low and only wins when nothing better exists.
    std::vector<PathPoint> path0;
    HazardReport h0;
    bool air_release_safe = false;
    bool air_reverse_safe = false;
    if (airborne_now) {
        GuardianInput zi = in;
        zi.stick_forward = 0.0;
        zi.stick_strafe = 0.0;
        zi.sprint = false;
        zi.jump = false;
        path0 = predict_path(zi);
        h0 = scanner_.scan_path(g, path0, in.health, in.already_fallen);
        air_release_safe = !is_danger(h0);
        GuardianInput ri = zi;
        ri.stick_forward = -in.stick_forward;
        ri.stick_strafe = -in.stick_strafe;
        air_reverse_safe = !is_danger(scanner_.scan_path(g, predict_path(ri), in.health, in.already_fallen));
    }
    const std::vector<PathPoint>& rpath = airborne_now ? path0 : path;
    const HazardReport& rh = airborne_now ? h0 : h;

    std::vector<GuardianOption> options;
    std::vector<GuardianPlan> details;

    // --- Halt: stop and sneak (sneaking also prevents walking off an edge).
    {
        GuardianOption o;
        o.action = GuardianAction::Halt;
        o.technique_id = "survival.sneak_halt";
        o.feasible = in.on_ground || air_release_safe;
        o.gain = in.on_ground ? (fall_hazard ? 0.97 : (t_leave >= 2 ? 0.95 : 0.70)) : 0.55;
        o.ticks_needed = 1;
        o.why = in.on_ground ? "stop all movement and sneak" : "release the stick in mid-air (air control limited, unverified)";
        GuardianPlan d;
        d.override_movement = true;
        d.sneak = true;
        options.push_back(o);
        details.push_back(d);
    }
    // --- Step back: reverse the input when the hazard is imminent.
    {
        GuardianOption o;
        o.action = GuardianAction::StepBack;
        o.technique_id = "survival.step_back";
        o.feasible = in.on_ground || air_reverse_safe;
        o.gain = in.on_ground ? (t_leave >= 1 ? 0.85 : 0.60) : 0.55;
        o.ticks_needed = 1;
        o.why = in.on_ground ? "reverse the current input" : "steer back in mid-air (air control limited, unverified)";
        GuardianPlan d;
        d.override_movement = true;
        d.move_forward = -in.stick_forward;
        d.move_strafe = -in.stick_strafe;
        options.push_back(o);
        details.push_back(d);
    }
    // --- Bridge forward: creep in sneak and fill the first missing cell on the human's route.
    if (ledge && in.kit.blocks > 0) {
        GuardianOption o;
        o.action = GuardianAction::BridgeForward;
        o.technique_id = "survival.bridge_forward";
        o.ticks_needed = cfg_.aim_ticks + 1;
        const PathPoint& q = path[hz - 1U];
        const PathPoint& p = path[hz];
        const int cy = fl(q.pos.y - 1e-6);
        const int cx = fl(p.pos.x);
        const int cz = fl(p.pos.z);
        GuardianPlan d;
        if (g.get(cx, cy, cz) == BlockClass::Air) {
            Support s = find_support(g, cx, cy, cz, eye, cfg_.placement_reach);
            if (s.ok) {
                o.feasible = true;
                o.gain = 0.93;
                o.why = "sneak-creep forward and place the missing block";
                d.override_movement = true;
                d.move_forward = in.stick_forward;
                d.move_strafe = in.stick_strafe;
                d.sneak = true;
                d.place_valid = true;
                d.place = s.order;
                d.aim_valid = true;
                aim_at(eye, s.order.click_point, &d.aim_yaw_deg, &d.aim_pitch_deg);
            } else {
                o.why = "no clickable face within reach / line of sight";
            }
        } else {
            o.why = "target cell is not empty air";
        }
        options.push_back(o);
        details.push_back(d);
    }
    // --- Block clutch: place a block under a falling player.
    if (airborne_now && in.kit.blocks > 0) {
        GuardianOption o;
        o.action = GuardianAction::BlockClutch;
        o.technique_id = "survival.block_clutch";
        o.ticks_needed = cfg_.aim_ticks + 1;
        GuardianPlan d;
        d.override_movement = true;  // release the stick so the player falls through the clutch column
        const int y0 = fl(in.feet.y);
        for (int k = 1; k <= 3 && !o.feasible; ++k) {
            const int cy = y0 - k;
            if (static_cast<double>(cy) + 1.0 > in.feet.y + 1e-6) continue;
            const int cx = fl(in.feet.x);
            const int cz = fl(in.feet.z);
            if (g.get(cx, cy, cz) != BlockClass::Air) continue;
            const Support s = find_support(g, cx, cy, cz, eye, cfg_.placement_reach);
            if (!s.ok) continue;
            o.feasible = true;
            o.gain = 0.80;
            o.why = "place a block under the falling player";
            d.place_valid = true;
            d.place = s.order;
            d.aim_valid = true;
            aim_at(eye, s.order.click_point, &d.aim_yaw_deg, &d.aim_pitch_deg);
        }
        if (!o.feasible) o.why = "no clickable face under the player";
        options.push_back(o);
        details.push_back(d);
    }
    // --- Water clutch: bucket onto the landing block.
    if (in.kit.water_bucket && (rh.kind == HazardKind::LethalFall || rh.kind == HazardKind::DamagingFall)) {
        GuardianOption o;
        o.action = GuardianAction::WaterClutch;
        o.technique_id = "survival.water_clutch";
        o.ticks_needed = 1;
        GuardianPlan d;
        d.override_movement = true;  // release the stick: fall straight onto the landing column
        if (rh.landing != BlockClass::Unknown && !rpath.empty()) {
            const PathPoint& L = rh.impact_tick >= 0 ? rpath[static_cast<std::size_t>(rh.impact_tick)] : rpath.back();
            const Vec3 click{static_cast<double>(fl(L.pos.x)) + 0.5, static_cast<double>(rh.landing_y) + 1.0,
                             static_cast<double>(fl(L.pos.z)) + 0.5};
            // The bucket only works inside reach.  At high fall speed one tick can skip the whole
            // window, so require that the predicted path actually spends a tick within reach.
            // Acting at tick i uses the state path[i] and takes effect before the step that produces
            // path[i+1], so the last useful tick is the one BEFORE the landing step.
            const std::size_t last = rh.impact_tick >= 1 ? static_cast<std::size_t>(rh.impact_tick) - 1U : rpath.size() - 1U;
            bool window = false;
            std::size_t window_idx = 0;
            for (std::size_t i = 0; i <= last && i < rpath.size() && !window; ++i) {
                const Vec3 e{rpath[i].pos.x, rpath[i].pos.y + cfg_.eye_height, rpath[i].pos.z};
                if (dist3(e, click) <= cfg_.water_reach) {
                    window = true;
                    window_idx = i;
                }
            }
            if (window) {
                o.feasible = true;
                o.gain = 0.92;
                o.ticks_needed = static_cast<int>(window_idx) + 1;
                o.why = "pour water on the landing block";
                d.aim_valid = true;
                aim_at(eye, click, &d.aim_yaw_deg, &d.aim_pitch_deg);
                d.place_valid = true;
                d.place.x = fl(L.pos.x);
                d.place.y = rh.landing_y + 1;
                d.place.z = fl(L.pos.z);
                d.place.support_x = d.place.x;
                d.place.support_y = rh.landing_y;
                d.place.support_z = d.place.z;
                d.place.face = 1;
                d.place.click_point = click;
                d.use_water = dist3(eye, click) <= cfg_.water_reach;  // armed until in reach
            } else {
                o.why = "fall speed skips the bucket's reach window";
            }
        } else {
            o.why = "landing block unknown";
        }
        options.push_back(o);
        details.push_back(d);
    }
    // --- Pearl escape: only with measured physics and enough health to pay for the landing.
    if (in.kit.ender_pearl) {
        GuardianOption o;
        o.action = GuardianAction::PearlEscape;
        o.technique_id = "survival.pearl_escape";
        o.ticks_needed = 3;
        GuardianPlan d;
        d.override_movement = true;  // hold still while the pearl is aimed and thrown
        if (!in.kit.pearl_calibrated || !in.kit.pearl_physics.calibrated) {
            o.why = "pearl physics are not calibrated: refusing to guess";
        } else if (in.health <= cfg_.pearl_self_damage + 1.0) {
            o.why = "health too low to survive the pearl landing";
        } else {
            struct Cand { int x, y, z; double d; };
            std::vector<Cand> cands;
            const int r = static_cast<int>(cfg_.pearl_range);
            const int bx = fl(in.feet.x);
            const int bz = fl(in.feet.z);
            const int by = fl(in.feet.y);
            for (int dx = -r; dx <= r; ++dx)
                for (int dz = -r; dz <= r; ++dz) {
                    const double dh = std::hypot(static_cast<double>(dx), static_cast<double>(dz));
                    if (dh > cfg_.pearl_range || dh < 3.0) continue;
                    for (int y = by - 8; y <= by + 4; ++y)
                        if (standable(g, bx + dx, y, bz + dz)) cands.push_back(Cand{bx + dx, y, bz + dz, dh + 0.1 * std::abs(y - by)});
                }
            std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
            const ProjectileSolver solver;
            for (std::size_t i = 0; i < cands.size() && i < 8U; ++i) {
                ProjectileRequest req;
                req.origin = eye;
                req.target_position = Vec3{static_cast<double>(cands[i].x) + 0.5, static_cast<double>(cands[i].y),
                                           static_cast<double>(cands[i].z) + 0.5};
                const ProjectileSolution sol = solver.solve(in.kit.pearl_physics, req);
                if (!sol.valid || sol.miss_distance > in.kit.pearl_physics.hit_tolerance) continue;
                o.feasible = true;
                o.gain = 0.85;
                o.why = "throw a pearl to a known safe landing";
                d.aim_valid = true;
                d.aim_yaw_deg = sol.yaw_degrees;
                d.aim_pitch_deg = sol.pitch_degrees;
                d.use_pearl = true;
                d.pearl_target = req.target_position;
                break;
            }
            if (!o.feasible) o.why = "no safe landing with a valid pearl solution";
        }
        options.push_back(o);
        details.push_back(d);
    }

    // --- Score and choose.
    const int budget = airborne_now ? (rh.impact_tick > 0 ? rh.impact_tick : cfg_.horizon_ticks) : std::max(1, t_leave);
    int best = -1;
    int best_any = -1;
    for (std::size_t i = 0; i < options.size(); ++i) {
        GuardianOption& o = options[i];
        double cost = 0.0;
        if (o.action == GuardianAction::PearlEscape) cost = cfg_.pearl_self_damage / std::max(in.health, 1.0) * 0.25;
        o.score = o.gain * eff(o.technique_id) - cost + (o.action == GuardianAction::BridgeForward ? cfg_.intent_bonus : 0.0);
        if (!o.feasible) continue;
        if (best_any < 0 || o.score > options[static_cast<std::size_t>(best_any)].score) best_any = static_cast<int>(i);
        if (o.ticks_needed <= budget && (best < 0 || o.score > options[static_cast<std::size_t>(best)].score))
            best = static_cast<int>(i);
    }
    if (best < 0) best = best_any;
    plan.options = options;
    if (best < 0) {
        plan.reason = std::string("danger (") + describe() + ") but no feasible rescue with the current kit";
        return plan;
    }
    const GuardianOption& chosen = options[static_cast<std::size_t>(best)];
    const GuardianPlan& d = details[static_cast<std::size_t>(best)];
    plan.action = chosen.action;
    plan.technique_id = chosen.technique_id;
    plan.override_movement = d.override_movement;
    plan.move_forward = d.move_forward;
    plan.move_strafe = d.move_strafe;
    plan.sprint = false;
    plan.sneak = d.sneak;
    plan.jump = false;
    plan.aim_valid = d.aim_valid;
    plan.aim_yaw_deg = d.aim_yaw_deg;
    plan.aim_pitch_deg = d.aim_pitch_deg;
    plan.place_valid = d.place_valid;
    plan.place = d.place;
    plan.use_pearl = d.use_pearl;
    plan.pearl_target = d.pearl_target;
    plan.use_water = d.use_water;
    plan.reason = std::string(describe()) + " predicted in " + std::to_string(t_leave) + " tick(s): " + chosen.why;
    return plan;
}

} // namespace bedrockai
