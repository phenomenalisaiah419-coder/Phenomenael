#include "core/control_hub.hpp"

#include <algorithm>
#include <cmath>

namespace bedrockai {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEye = 1.62;

int fl(double v) { return static_cast<int>(std::floor(v)); }
double wrap180(double d) {
    while (d > 180.0) d -= 360.0;
    while (d <= -180.0) d += 360.0;
    return d;
}
double angle_diff(double a, double b) { return std::fabs(wrap180(a - b)); }
double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
double limit_turn(double cur, double target, double max_step) {
    return wrap180(cur + clamp(wrap180(target - cur), -max_step, max_step));
}
void world_to_stick(double wx, double wz, double yaw_deg, double* fwd, double* str) {
    const double y = yaw_deg * kPi / 180.0;
    *fwd = wx * -std::sin(y) + wz * std::cos(y);
    *str = wx * std::cos(y) + wz * std::sin(y);
}
double heading_of(double wx, double wz) { return std::atan2(-wx, wz) * 180.0 / kPi; }

} // namespace

ControlHub::ControlHub(HubConfig config)
    : cfg_(config),
      authority_(config.takeover),
      learner_(config.learner),
      opponents_(config.opponents),
      items_(config.items),
      guardian_(config.guardian) {}

HubCommand ControlHub::passthrough(const HubInput& in) const {
    HubCommand c;
    c.move_forward = std::isfinite(in.human.move_y) ? clamp(in.human.move_y, -1.0, 1.0) : 0.0;
    c.move_strafe = std::isfinite(in.human.move_x) ? clamp(in.human.move_x, -1.0, 1.0) : 0.0;
    c.sprint = in.human.sprint_held;
    c.sneak = in.human.sneak_held;
    c.jump = in.human.jump_held;
    return c;
}

const HubEnemy* ControlHub::pick_target(const HubInput& in) const {
    const HubEnemy* best = nullptr;
    double best_d = cfg_.engage_range;
    for (const HubEnemy& e : in.enemies) {
        if (!e.hostile || e.health <= 0.0) continue;
        const double d = std::hypot(e.pos.x - in.feet.x, e.pos.z - in.feet.z);
        if (d <= best_d && (best == nullptr || d < best_d || (d == best_d && e.id < best->id))) {
            best = &e;
            best_d = d;
        }
    }
    return best;
}

SkillContext ControlHub::combat_context(const HubInput& in, const std::string& opp) const {
    SkillContext c;
    c.server_key = in.server_key;
    c.situation = cfg_.situation_pvp;
    c.opponent_id = opp;
    const OpponentProfile p = opponents_.profile(opp);
    if (p.archetype != Archetype::Unknown) c.archetype = archetype_name(p.archetype);
    return c;
}

bool ControlHub::next_bridge_cell(const HubInput& in, int* cx, int* cy, int* cz) const {
    if (!in.grid || !in.on_ground) return false;
    const PredictedIntent& pi = predictor_.current();
    const double h = (pi.moving ? pi.heading_deg : (have_intent_yaw_ ? intent_yaw_ : in.yaw_deg)) * kPi / 180.0;
    const double fx = -std::sin(h);
    const double fz = std::cos(h);
    int sx = 0;
    int sz = 0;
    if (std::fabs(fx) >= 2.0 * std::fabs(fz)) sx = fx > 0.0 ? 1 : -1;
    else if (std::fabs(fz) >= 2.0 * std::fabs(fx)) sz = fz > 0.0 ? 1 : -1;
    else {
        sx = fx > 0.0 ? 1 : -1;
        sz = fz > 0.0 ? 1 : -1;
    }
    const int px = fl(in.feet.x);
    const int pz = fl(in.feet.z);
    const int py = fl(in.feet.y - 1e-6);
    if (!block_is_support(in.grid->get(px, py, pz))) return false;
    for (int k = 1; k <= 6; ++k) {
        const int xs = (sx != 0 && sz != 0) ? (k + 1) / 2 : k;
        const int zs = (sx != 0 && sz != 0) ? k / 2 : k;
        const int x = px + sx * xs;
        const int z = pz + sz * zs;
        const BlockClass cls = in.grid->get(x, py, z);
        if (block_is_support(cls)) continue;
        if (cls != BlockClass::Air) return false;
        *cx = x;
        *cy = py;
        *cz = z;
        return true;
    }
    return false;
}

void ControlHub::apply_guardian(const HubInput& in, const GuardianPlan& plan, HubOutput* out) {
    HubCommand& cmd = out->cmd;
    bool aligned = false;
    if (plan.aim_valid) {
        cmd.look_override = true;
        cmd.look_yaw_deg = plan.aim_yaw_deg;      // emergencies snap: speed matters more than looks
        cmd.look_pitch_deg = clamp(plan.aim_pitch_deg, -90.0, 90.0);
        aligned = cfg_.emergency_same_tick_aim ||
                  (angle_diff(in.yaw_deg, plan.aim_yaw_deg) <= cfg_.aim_tolerance_deg &&
                   std::fabs(in.pitch_deg - plan.aim_pitch_deg) <= cfg_.aim_tolerance_deg);
    }
    if (plan.override_movement) {
        movement_from_human_ = false;
        // plan.move_* are expressed in the world-aligned frame (yaw 0): strafe = +X, forward = +Z.
        const double commanded_yaw = cmd.look_override ? cmd.look_yaw_deg : in.yaw_deg;
        world_to_stick(plan.move_strafe, plan.move_forward, commanded_yaw, &cmd.move_forward, &cmd.move_strafe);
        cmd.move_forward = clamp(cmd.move_forward, -1.0, 1.0);
        cmd.move_strafe = clamp(cmd.move_strafe, -1.0, 1.0);
        cmd.sprint = false;
        cmd.sneak = plan.sneak;
        cmd.jump = false;
    }
    AiProposal prop;
    prop.mode = ControlMode::Emergency;
    prop.priority = Priority::ImmediateSurvival;
    prop.emergency = true;
    const bool block_action = plan.action == GuardianAction::BridgeForward || plan.action == GuardianAction::BlockClutch;
    if (plan.place_valid && aligned && block_action) prop.place = Action{ActionKind::PlaceBlock, true, UtilityKind::None};
    if (plan.use_pearl && aligned) prop.utility = Action{ActionKind::Pearl, true, UtilityKind::Pearl};
    InventoryState inv;
    inv.has_pearl = in.kit.ender_pearl;
    const ControlFrame cf = arbitrate(caps_, inv, HumanInput{in.human, -1}, prop);
    if (prop.place) {
        if (cf.place) {
            cmd.place = true;
            cmd.place_order = plan.place;
        } else {
            out->denials.push_back("emergency placement denied: " + cf.denial_reason);
        }
    }
    if (prop.utility) {
        if (cf.utility) {
            cmd.throw_pearl = true;
            cmd.pearl_target = plan.pearl_target;
        } else {
            out->denials.push_back("emergency pearl denied");
        }
    }
    if (plan.use_water && aligned && plan.action == GuardianAction::WaterClutch) {
        if (caps_.survival_override_can_use_utilities && !in.human.explicit_override) {
            cmd.use_water = true;
            cmd.water_order = plan.place;
        } else {
            out->denials.push_back("emergency bucket denied");
        }
    }
    out->technique = plan.technique_id;
    out->explanation = plan.reason;
}

void ControlHub::end_segment(const HubInput& in, bool force) {
    if (technique_.empty()) return;
    const bool timed_out = in.tick - segment_start_ >= static_cast<std::uint64_t>(cfg_.segment_ticks);
    if (!force && !timed_out) return;
    const double engaged = segment_dealt_ + segment_taken_;
    double reward = clamp(0.5 + (segment_dealt_ - segment_taken_) / 20.0, 0.0, 1.0);
    bool success = reward >= cfg_.success_reward;
    if (technique_ == "combat.retreat_reset") {
        success = segment_taken_ < 1.0;
        reward = success ? 0.8 : 0.3;
    } else if (engaged <= 0.0) {
        if (!segment_attack_input_) {
            // The human never authorised an attack, so "no contact" says nothing about the technique.
            technique_.clear();
            segment_target_.clear();
            segment_has_target_health_ = false;
            return;
        }
        success = false;  // the tactic never produced contact inside the window
        reward = 0.4;
    }
    SkillOutcome o;
    o.skill = technique_;
    o.context = combat_context(in, segment_target_);
    o.context.critical = technique_ == "combat.retreat_reset";
    o.success = success;
    o.reward = reward;
    o.tick = in.tick;
    learner_.record(o);
    technique_.clear();
    segment_target_.clear();
    segment_has_target_health_ = false;
}

void ControlHub::drive_combat(const HubInput& in, const HubEnemy& target, HubOutput* out) {
    HubCommand& cmd = out->cmd;
    const double hp_frac = in.max_health > 0.0 ? in.health / in.max_health : 1.0;
    const bool retreat_needed = hp_frac < cfg_.retreat_health_fraction;
    if (!technique_.empty() && (segment_target_ != target.id || (retreat_needed && technique_ != "combat.retreat_reset")))
        end_segment(in, true);
    else
        end_segment(in, false);
    if (technique_.empty()) {
        SkillContext ctx = combat_context(in, target.id);
        std::vector<SkillCandidate> cands;
        if (retreat_needed) {
            ctx.critical = true;
            cands = {{"combat.retreat_reset", 0.7, 0.9}};
        } else {
            const OpponentProfile prof = opponents_.profile(target.id);
            // Heuristic priors only: the learner's evidence overrides them as outcomes come in.
            double p_press = 0.5, p_orbit = 0.5, p_kite = 0.5;
            switch (prof.archetype) {
                case Archetype::Rusher: p_press = 0.40; p_orbit = 0.55; p_kite = 0.60; break;
                case Archetype::Strafer: p_press = 0.45; p_orbit = 0.50; p_kite = 0.50; break;
                case Archetype::Defensive: p_press = 0.65; p_orbit = 0.50; p_kite = 0.40; break;
                case Archetype::Evasive: p_press = 0.65; p_orbit = 0.40; p_kite = 0.40; break;
                default: break;
            }
            // The heuristic is the PRIOR belief (worth ~3 observations); the situation adds no extra bias,
            // so the first few outcomes can already overrule it.
            cands = {{"combat.pressure_chase", p_press, 0.5}, {"combat.strafe_orbit", p_orbit, 0.5}, {"combat.kite_back", p_kite, 0.5}};
        }
        const SkillChoice pick = learner_.choose(cands, ctx, in.tick);
        technique_ = pick.skill;
        segment_target_ = target.id;
        segment_start_ = in.tick;
        segment_dealt_ = 0.0;
        segment_taken_ = 0.0;
        segment_target_health_ = target.health;
        segment_has_target_health_ = true;
        segment_attack_input_ = false;
        out->explanation = "chose " + pick.skill + " (" + pick.why + ")";
    }

    if (in.human.attack_held) segment_attack_input_ = true;
    const double reach_for_geometry = cfg_.attack_reach.value_or(3.0);
    const double dx = target.pos.x - in.feet.x;
    const double dz = target.pos.z - in.feet.z;
    const double d = std::max(1e-6, std::hypot(dx, dz));
    const double ux = dx / d;
    const double uz = dz / d;

    Vec3 aim_point = target.pos;
    if (opponents_.profile(target.id).samples >= cfg_.opponents.min_samples) aim_point = opponents_.predict_position(target.id, 3);
    aim_point.y = target.pos.y + 1.0;
    const Vec3 eye{in.feet.x, in.feet.y + kEye, in.feet.z};
    double aim_yaw = 0.0;
    double aim_pitch = 0.0;
    aim_at(eye, aim_point, &aim_yaw, &aim_pitch);
    cmd.look_override = true;
    cmd.look_yaw_deg = limit_turn(in.yaw_deg, aim_yaw, cfg_.max_turn_deg_per_tick);
    cmd.look_pitch_deg = clamp(aim_pitch, -60.0, 60.0);

    double wx = 0.0;
    double wz = 0.0;
    bool sprint = false;
    if (technique_ == "combat.pressure_chase") {
        wx = ux;
        wz = uz;
        sprint = d > 2.0;
    } else if (technique_ == "combat.strafe_orbit") {
        const double d_star = reach_for_geometry * 0.85;
        const double radial = clamp((d - d_star) / 2.0, -1.0, 1.0);
        const int phase = static_cast<int>((in.tick - segment_start_) / 14U);
        const double lat = (phase % 2 == 0) ? 1.0 : -1.0;
        wx = radial * ux + 0.9 * lat * -uz;
        wz = radial * uz + 0.9 * lat * ux;
    } else if (technique_ == "combat.kite_back") {
        if (d < reach_for_geometry * 0.7) {
            wx = -ux;
            wz = -uz;
        } else if (d > reach_for_geometry * 0.95) {
            wx = 0.6 * ux;
            wz = 0.6 * uz;
        }
    } else {  // combat.retreat_reset
        wx = -ux;
        wz = -uz;
        sprint = true;
    }
    const double mag = std::hypot(wx, wz);
    if (mag > 1.0) {
        wx /= mag;
        wz /= mag;
    }
    movement_from_human_ = false;
    world_to_stick(wx, wz, cmd.look_yaw_deg, &cmd.move_forward, &cmd.move_strafe);
    cmd.sprint = sprint;
    cmd.sneak = false;
    cmd.jump = false;
    if (std::hypot(wx, wz) > 0.05) last_ai_heading_ = heading_of(wx, wz);
    else last_ai_heading_.reset();

    const bool reach_known = cfg_.attack_reach.has_value();
    const bool in_reach = reach_known && d <= *cfg_.attack_reach;
    const bool aimed = angle_diff(cmd.look_yaw_deg, aim_yaw) <= cfg_.aim_tolerance_deg;
    const bool cadence_ok = !attacked_once_ || in.tick - last_attack_tick_ >= static_cast<std::uint64_t>(cfg_.attack_interval_ticks);
    if (technique_ != "combat.retreat_reset" && in_reach && aimed && cadence_ok) {
        AiProposal prop;
        prop.mode = ControlMode::Takeover;
        prop.priority = Priority::Combat;
        prop.attack = Action{ActionKind::Attack, false, UtilityKind::None};
        const ControlFrame cf = arbitrate(caps_, InventoryState{}, HumanInput{in.human, -1}, prop);
        if (cf.attack) {
            cmd.attack = true;
            last_attack_tick_ = in.tick;
            attacked_once_ = true;
        } else {
            out->denials.push_back("attack needs the human's attack input");
        }
    } else if (!reach_known && d < 4.0) {
        out->denials.push_back("attack disabled: no measured attack reach");
    }
    out->technique = technique_;
    if (out->explanation.empty()) out->explanation = technique_;
}

bool ControlHub::drive_bridge(const HubInput& in, HubOutput* out) {
    int cx = 0, cy = 0, cz = 0;
    if (in.kit.blocks <= 0 || !next_bridge_cell(in, &cx, &cy, &cz)) return false;
    HubCommand& cmd = out->cmd;
    const PredictedIntent& pi = predictor_.current();
    const double heading = pi.moving ? pi.heading_deg : (have_intent_yaw_ ? intent_yaw_ : in.yaw_deg);
    const double hr = heading * kPi / 180.0;
    const bool diagonal = std::fabs(std::sin(hr)) > 0.38 && std::fabs(std::cos(hr)) > 0.38;
    technique_.clear();
    out->technique = diagonal ? "bridge.sneak_diagonal" : "bridge.sneak_straight";

    const Vec3 eye{in.feet.x, in.feet.y + kEye, in.feet.z};
    PlacementOrder order;
    const bool have = find_placement(*in.grid, cx, cy, cz, eye, cfg_.guardian.placement_reach, &order);
    double wx = -std::sin(hr);
    double wz = std::cos(hr);
    const double wm = std::hypot(wx, wz);
    if (wm > 1e-9) {
        wx /= wm;
        wz /= wm;
    }
    double aim_yaw = in.yaw_deg;
    double aim_pitch = in.pitch_deg;
    if (have) {
        aim_at(eye, order.click_point, &aim_yaw, &aim_pitch);
        cmd.look_override = true;
        cmd.look_yaw_deg = limit_turn(in.yaw_deg, aim_yaw, cfg_.max_turn_deg_per_tick);
        cmd.look_pitch_deg = clamp(aim_pitch, -90.0, 90.0);
    }
    const double ref_yaw = cmd.look_override ? cmd.look_yaw_deg : in.yaw_deg;
    movement_from_human_ = false;
    world_to_stick(wx, wz, ref_yaw, &cmd.move_forward, &cmd.move_strafe);
    cmd.sneak = true;  // sneaking is what keeps this safe: the player cannot walk off the edge
    cmd.sprint = false;
    cmd.jump = false;
    last_ai_heading_ = heading;
    const bool aimed = have && angle_diff(in.yaw_deg, aim_yaw) <= cfg_.aim_tolerance_deg &&
                       std::fabs(in.pitch_deg - aim_pitch) <= cfg_.aim_tolerance_deg;
    if (aimed) {
        AiProposal prop;
        prop.mode = ControlMode::Takeover;
        prop.priority = Priority::Building;
        prop.place = Action{ActionKind::PlaceBlock, false, UtilityKind::None};
        const ControlFrame cf = arbitrate(caps_, InventoryState{}, HumanInput{in.human, -1}, prop);
        if (cf.place) {
            cmd.place = true;
            cmd.place_order = order;
        } else {
            out->denials.push_back("placement needs the human's place input");
        }
    }
    out->explanation = out->technique + (have ? ": aiming at the next block" : ": creeping to the edge");
    return true;
}

void ControlHub::note_guardian_episode(const HubInput& in, const HubOutput& out) {
    const bool active = out.authority.emergency && out.guardian.intervene;
    if (active && !episode_active_) {
        episode_active_ = true;
        episode_start_health_ = in.health;
        episode_technique_ = out.guardian.technique_id;
        episode_server_ = in.server_key;
    } else if (active) {
        if (!out.guardian.technique_id.empty()) episode_technique_ = out.guardian.technique_id;
    } else if (episode_active_ && !out.authority.emergency) {
        const double lost = episode_start_health_ - in.health;
        const bool ok = lost < 3.0 && in.health > 0.0;
        if (!episode_technique_.empty()) {
            SkillOutcome o;
            o.skill = episode_technique_;
            o.context.server_key = episode_server_;
            o.context.situation = "survival";
            o.context.critical = true;
            o.success = ok;
            o.reward = ok ? clamp(1.0 - lost / 20.0, 0.5, 1.0) : 0.1;
            o.tick = in.tick;
            learner_.record(o);
        }
        episode_active_ = false;
        episode_technique_.clear();
    }
}

HubOutput ControlHub::tick(const HubInput& in) {
    HubOutput out;
    out.cmd = passthrough(in);
    movement_from_human_ = true;

    // 0. Fair-play gate: what is the AI allowed to do on this server?
    const AssistLevel level = policy_.effective(in.server_key, in.server_class, in.requested_level);
    out.effective_level = level;
    if (level != in.requested_level)
        out.denials.push_back(std::string("policy limits the assist level to ") + assist_level_name(level));
    const UserMode mode = AutomationPolicy::to_user_mode(level);
    if (!mode_initialised_ || mode != authority_.user_mode()) {
        authority_.set_user_mode(mode);
        mode_initialised_ = true;
        last_ai_driving_ = false;
        last_ai_heading_.reset();
    }

    // 1. Observations (allowed at every level, including ObserveOnly).
    if (!have_intent_yaw_ || !last_cmd_.look_override) {
        // The game applies the human's camera delta BEFORE it interprets the move vector, so the
        // direction this tick's stick means is relative to the yaw AFTER the delta.
        const double d = std::isfinite(in.human.look_dx) ? in.human.look_dx * cfg_.look_to_degrees : 0.0;
        intent_yaw_ = wrap180(in.yaw_deg + d);
        have_intent_yaw_ = true;
    }
    predictor_.observe(in.tick, in.human, intent_yaw_);
    if (!have_last_y_) {
        last_y_ = in.feet.y;
        have_last_y_ = true;
    }
    if (in.on_ground) fall_distance_ = 0.0;
    else if (in.feet.y < last_y_) fall_distance_ += last_y_ - in.feet.y;
    last_y_ = in.feet.y;
    const double health_delta = have_last_health_ ? in.health - last_health_ : 0.0;
    last_health_ = in.health;
    have_last_health_ = true;
    for (const HubEnemy& e : in.enemies)
        if (e.hostile) opponents_.observe(e.id, in.tick, e.pos, in.feet, false);
    if (!technique_.empty()) {
        if (health_delta < 0.0) segment_taken_ += -health_delta;
        for (const HubEnemy& e : in.enemies) {
            if (e.id != segment_target_) continue;
            if (segment_has_target_health_ && e.health < segment_target_health_) segment_dealt_ += segment_target_health_ - e.health;
            segment_target_health_ = e.health;
            segment_has_target_health_ = true;
        }
    }

    // 2. Survival look-ahead on the inputs expected to persist.
    GuardianPlan plan;
    if (mode != UserMode::Off && in.grid) {
        GuardianInput gi;
        gi.grid = in.grid;
        gi.feet = in.feet;
        gi.vertical_velocity = in.vertical_velocity;
        gi.on_ground = in.on_ground;
        gi.pitch_deg = in.pitch_deg;
        gi.health = in.health;
        gi.already_fallen = fall_distance_;
        gi.kit = in.kit;
        // The guardian works in a WORLD-aligned frame (yaw 0: forward = +Z, strafe = +X) so that its
        // movement override means the same thing whatever the camera is doing.
        double sf = 0.0, ss = 0.0, ref_yaw = intent_yaw_;
        if (last_ai_driving_) {
            sf = last_cmd_.move_forward;
            ss = last_cmd_.move_strafe;
            ref_yaw = last_cmd_.look_override ? last_cmd_.look_yaw_deg : in.yaw_deg;
            gi.sprint = last_cmd_.sprint && sf > 0.0;  // sprint needs forward input in the camera frame
            gi.sneak = last_cmd_.sneak;
            gi.jump = last_cmd_.jump;
        } else {
            sf = std::isfinite(in.human.move_y) ? in.human.move_y : 0.0;  // relative to THEIR camera
            ss = std::isfinite(in.human.move_x) ? in.human.move_x : 0.0;
            gi.sprint = in.human.sprint_held && sf > 0.0;
            gi.sneak = in.human.sneak_held;
            gi.jump = in.human.jump_held;
        }
        const double ry = ref_yaw * kPi / 180.0;
        gi.yaw_deg = 0.0;
        gi.stick_forward = sf * std::cos(ry) + ss * std::sin(ry);   // world +Z component
        gi.stick_strafe = sf * -std::sin(ry) + ss * std::cos(ry);   // world +X component
        SkillContext survival_ctx;
        survival_ctx.server_key = in.server_key;
        survival_ctx.situation = "survival";
        survival_ctx.critical = true;
        gi.effectiveness = [this, survival_ctx](const std::string& id) { return learner_.mean(id, survival_ctx, 0.75); };
        plan = guardian_.evaluate(gi);
    }
    out.guardian = plan;

    // 3. Does the AI have something to drive?
    const HubEnemy* target = pick_target(in);
    int bx = 0, by = 0, bz = 0;
    const bool bridge_need = in.kit.blocks > 0 && predictor_.current().kind == IntentKind::PlaceHold &&
                             next_bridge_cell(in, &bx, &by, &bz);
    AuthorityInput ain;
    ain.tick = in.tick;
    ain.human = in.human;
    ain.predicted = predictor_.current();
    ain.player_yaw_deg = intent_yaw_;
    ain.ai_has_plan = bridge_need || target != nullptr;
    if (last_ai_driving_) ain.ai_heading_deg = last_ai_heading_;
    ain.emergency_request = plan.intervene && mode != UserMode::Off;
    out.authority = authority_.update(ain);
    const AuthorityDecision& auth = out.authority;

    // 4. Compose the command.
    bool ai_drove = false;
    bool drove_combat = false;
    if (auth.emergency && plan.intervene) {
        apply_guardian(in, plan, &out);
    } else if (auth.ai_moves && !auth.emergency) {
        if (bridge_need) {
            ai_drove = drive_bridge(in, &out);
        } else if (target != nullptr) {
            drive_combat(in, *target, &out);
            ai_drove = true;
            drove_combat = true;
        }
    }
    if (!ai_drove) last_ai_heading_.reset();
    if (out.cmd.look_override && movement_from_human_) {
        // The AI turned the camera but the human still steers: keep the human's intended WORLD
        // direction by re-expressing the stick against the commanded yaw.
        const double iy = intent_yaw_ * kPi / 180.0;
        const double wx = out.cmd.move_forward * -std::sin(iy) + out.cmd.move_strafe * std::cos(iy);
        const double wz = out.cmd.move_forward * std::cos(iy) + out.cmd.move_strafe * std::sin(iy);
        world_to_stick(wx, wz, out.cmd.look_yaw_deg, &out.cmd.move_forward, &out.cmd.move_strafe);
    }

    // 5. Learning bookkeeping.
    if (auth.human_took_control && !technique_.empty()) {
        // The human changed the plan: that is direct negative feedback on the running technique.
        SkillOutcome o;
        o.skill = technique_;
        o.context = combat_context(in, segment_target_);
        o.success = false;
        o.reward = 0.3;
        o.tick = in.tick;
        learner_.record(o);
        technique_.clear();
        segment_target_.clear();
        segment_has_target_health_ = false;
    } else if (!drove_combat && !technique_.empty()) {
        end_segment(in, true);
    }
    note_guardian_episode(in, out);

    last_ai_driving_ = ai_drove && auth.ai_moves && !auth.emergency;
    last_cmd_ = out.cmd;
    if (out.explanation.empty()) out.explanation = auth.reason;
    return out;
}

} // namespace bedrockai
