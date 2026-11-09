#pragma once
// Shared closed-loop rig for the v0.35 tests: a SimWorld + ControlHub wired together.
#include "core/control_hub.hpp"
#include "core/sim_world.hpp"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace simrig {
using namespace bedrockai;

constexpr double kFaceX = -90.0;  // yaw whose forward vector is +X

inline ProjectilePhysics pearl_physics() {
    ProjectilePhysics p;
    p.launch_speed = 30.0;
    p.gravity = 12.0;
    p.drag_per_tick = 0.99;
    p.tick_rate = 20.0;
    p.max_flight_seconds = 4.0;
    p.hit_tolerance = 0.6;
    p.calibrated = true;
    return p;
}

inline void platform(WorldGrid& g, int x0, int z0, int x1, int z1, int y = 63) { g.fill(x0, y, z0, x1, y, z1, BlockClass::Solid); }

struct Rig {
    SimWorld world;
    ControlHub hub;
    AssistLevel level = AssistLevel::Takeover;
    ServerClass cls = ServerClass::OwnWorld;
    std::function<HumanIntentFrame(std::uint64_t)> human;
    bool pearl_calibrated = false;
    HubOutput last;
    std::set<std::string> techniques;
    std::vector<Authority> states;
    int emergency_ticks = 0;
    bool human_took = false;

    static HubConfig config() {
        HubConfig c;
        c.attack_reach = 3.0;
        return c;
    }
    explicit Rig(HubConfig hc = config(), SimConfig sc = {}) : world(sc), hub(hc) {
        world.grid.mark_region_loaded(-64, -64, 96, 96);
        world.player.pos = Vec3{3.5, 64.0, 3.5};
        world.player.yaw = kFaceX;
    }
    HubInput input(const HumanIntentFrame& h) const {
        HubInput in;
        in.tick = world.tick;
        in.feet = world.player.pos;
        in.vertical_velocity = world.player.vy;
        in.on_ground = world.player.on_ground;
        in.yaw_deg = world.player.yaw;
        in.pitch_deg = world.player.pitch;
        in.health = world.player.health;
        in.max_health = 20.0;
        in.grid = &world.grid;
        in.human = h;
        in.kit.blocks = world.player.blocks;
        in.kit.ender_pearl = world.player.pearls > 0;
        in.kit.pearl_calibrated = pearl_calibrated;
        in.kit.pearl_physics = pearl_physics();
        in.kit.water_bucket = world.player.water_bucket;
        for (const SimEnemy& e : world.enemies) in.enemies.push_back(HubEnemy{e.id, e.pos, e.health, true});
        in.server_key = "sim|test";
        in.server_class = cls;
        in.requested_level = level;
        return in;
    }
    void step() {
        const HumanIntentFrame h = human ? human(world.tick) : HumanIntentFrame{};
        last = hub.tick(input(h));
        const HubCommand& c = last.cmd;
        SimInput si;
        si.forward = c.move_forward;
        si.strafe = c.move_strafe;
        si.sprint = c.sprint;
        si.sneak = c.sneak;
        si.jump = c.jump;
        si.yaw_deg = c.look_override ? c.look_yaw_deg : world.player.yaw + h.look_dx;
        si.pitch_deg = c.look_override ? c.look_pitch_deg : world.player.pitch - h.look_dy;
        si.attack = c.attack || h.attack_held;
        si.place = c.place;
        si.place_order = c.place_order;
        si.throw_pearl = c.throw_pearl;
        si.pearl_target = c.pearl_target;
        si.use_water = c.use_water;
        si.water_order = c.water_order;
        if (!last.technique.empty()) techniques.insert(last.technique);
        if (last.authority.emergency) ++emergency_ticks;
        if (last.authority.human_took_control) human_took = true;
        states.push_back(last.authority.state);
        world.step(si);
    }
    void run(int n) {
        for (int i = 0; i < n && !world.player.dead; ++i) step();
    }
};

inline HumanIntentFrame run_forward() {
    HumanIntentFrame f;
    f.move_y = 1.0;
    f.sprint_held = true;
    return f;
}


} // namespace simrig
