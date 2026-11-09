#include "core/sim_world.hpp"

#include <algorithm>
#include <cmath>

namespace bedrockai {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEye = 1.62;
int fl(double v) { return static_cast<int>(std::floor(v)); }
double dist3(const Vec3& a, const Vec3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
double wrap180(double d) {
    while (d > 180.0) d -= 360.0;
    while (d <= -180.0) d += 360.0;
    return d;
}
} // namespace

SimWorld::SimWorld(SimConfig config) : grid(-64, 319), cfg_(config) {}

bool SimWorld::standing_on(BlockClass c) const {
    return grid.get(fl(player.pos.x), fl(player.pos.y - 1e-6) , fl(player.pos.z)) == c ||
           grid.get(fl(player.pos.x), fl(player.pos.y - 1e-6) - 1, fl(player.pos.z)) == c;
}

bool SimWorld::look_matches(const Vec3& eye, const Vec3& point, double yaw, double pitch, double tol) const {
    double want_yaw = 0.0;
    double want_pitch = 0.0;
    aim_at(eye, point, &want_yaw, &want_pitch);
    const double dyaw = std::fabs(wrap180(want_yaw - yaw));
    const double dpitch = std::fabs(want_pitch - pitch);
    return dyaw <= tol && dpitch <= tol;
}

void SimWorld::hurt(double amount, const char* cause) {
    if (player.dead || amount <= 0.0) return;
    player.health -= amount;
    damage_taken += amount;
    if (player.health <= 0.0) {
        player.health = 0.0;
        player.dead = true;
        player.death_cause = cause;
    }
}

void SimWorld::apply_actions(const SimInput& in) {
    const Vec3 eye{player.pos.x, player.pos.y + kEye, player.pos.z};
    auto reject = [&](const std::string& why) { rejected.push_back(why); };
    auto overlaps_player = [&](int x, int y, int z) {
        const double hw = 0.3;
        return static_cast<double>(x) < player.pos.x + hw && static_cast<double>(x) + 1.0 > player.pos.x - hw &&
               static_cast<double>(z) < player.pos.z + hw && static_cast<double>(z) + 1.0 > player.pos.z - hw &&
               static_cast<double>(y) < player.pos.y + 1.8 && static_cast<double>(y) + 1.0 > player.pos.y;
    };
    auto adjacent = [](const PlacementOrder& o) {
        return std::abs(o.x - o.support_x) + std::abs(o.y - o.support_y) + std::abs(o.z - o.support_z) == 1;
    };

    if (in.place) {
        const PlacementOrder& o = in.place_order;
        if (player.blocks <= 0) reject("place: no blocks");
        else if (grid.get(o.x, o.y, o.z) != BlockClass::Air) reject("place: target cell is not empty air");
        else if (!adjacent(o)) reject("place: support block is not adjacent");
        else if (grid.get(o.support_x, o.support_y, o.support_z) != BlockClass::Solid &&
                 grid.get(o.support_x, o.support_y, o.support_z) != BlockClass::Bounce) reject("place: support is not a solid block");
        else if (dist3(eye, o.click_point) > cfg_.place_reach) reject("place: out of reach");
        else if (!look_matches(eye, o.click_point, in.yaw_deg, in.pitch_deg, cfg_.aim_tolerance_deg)) reject("place: not aimed at the face");
        else if (overlaps_player(o.x, o.y, o.z)) reject("place: target inside the player");
        else {
            grid.set(o.x, o.y, o.z, BlockClass::Solid);
            --player.blocks;
            ++blocks_placed;
        }
    }
    if (in.use_water) {
        const PlacementOrder& o = in.water_order;
        if (!player.water_bucket) reject("water: no bucket");
        else if (grid.get(o.x, o.y, o.z) != BlockClass::Air) reject("water: target cell is not empty air");
        else if (dist3(eye, o.click_point) > cfg_.place_reach) reject("water: out of reach");
        else if (!look_matches(eye, o.click_point, in.yaw_deg, in.pitch_deg, cfg_.aim_tolerance_deg)) reject("water: not aimed at the target");
        else {
            grid.set(o.x, o.y, o.z, BlockClass::Water);
            player.water_bucket = false;
        }
    }
    if (in.throw_pearl) {
        if (player.pearls <= 0) reject("pearl: none left");
        else if (pearl_.active) reject("pearl: one already in flight");
        else {
            const Vec3 target = in.pearl_target;
            double want_yaw = 0.0;
            double want_pitch = 0.0;
            aim_at(eye, target, &want_yaw, &want_pitch);
            if (std::fabs(wrap180(want_yaw - in.yaw_deg)) > 6.0) {
                reject("pearl: not aimed at the target");
            } else {
                --player.pearls;
                pearl_.active = true;
                pearl_.target = target;
                const double d = dist3(eye, target);
                pearl_.ticks_left = std::max(3, static_cast<int>(std::ceil(d / cfg_.pearl_speed)));
            }
        }
    }
}

void SimWorld::move_player(const SimInput& in) {
    if (knock_ticks_ > 0) {
        const double nx = player.pos.x + knock_.x;
        const double nz = player.pos.z + knock_.z;
        const BlockClass feet = grid.get(fl(nx), fl(player.pos.y + 0.01), fl(nz));
        const BlockClass head = grid.get(fl(nx), fl(player.pos.y + 1.01), fl(nz));
        if (!block_is_support(feet) && !block_is_support(head)) {
            player.pos.x = nx;
            player.pos.z = nz;
        }
        --knock_ticks_;
    }
    const std::vector<PathPoint> next =
        motion_.predict(grid, player.pos, player.vy, player.on_ground, in.yaw_deg, in.forward, in.strafe,
                        in.sprint && in.forward > 0.0, in.sneak, in.jump, 1);
    if (next.size() < 2U) return;
    const PathPoint& np = next[1];
    const double prev_y = player.pos.y;
    player.pos = np.pos;
    player.vy = np.vy;
    player.on_ground = !np.airborne;
    if (np.airborne && np.pos.y < prev_y) player.fall_distance += prev_y - np.pos.y;
    if (!np.airborne && np.landed != BlockClass::Unknown) {
        switch (np.landed) {
            case BlockClass::Lava:
                hurt(1000.0, "lava");
                break;
            case BlockClass::Water:
            case BlockClass::Web:
            case BlockClass::Climbable:
            case BlockClass::Bounce:
                break;
            default: {
                const double dmg = std::max(0.0, std::ceil(player.fall_distance - 3.0));
                if (dmg > 0.0) hurt(dmg, "fall");
                if (np.landed == BlockClass::Hazard) hurt(1.0, "harmful block");
            }
        }
        player.fall_distance = 0.0;
    }
    if (grid.get(fl(player.pos.x), fl(player.pos.y + 0.01), fl(player.pos.z)) == BlockClass::Lava) hurt(1000.0, "lava");
    if (grid.get(fl(player.pos.x), fl(player.pos.y + 0.01), fl(player.pos.z)) == BlockClass::Water) player.fall_distance = 0.0;
    if (player.pos.y < cfg_.void_kill_y) hurt(1000.0, "void");
    player.yaw = in.yaw_deg;
    player.pitch = in.pitch_deg;
}

void SimWorld::move_enemies() {
    for (SimEnemy& e : enemies) {
        if (!e.alive()) continue;
        double dx = player.pos.x - e.pos.x;
        double dz = player.pos.z - e.pos.z;
        const double dist = std::hypot(dx, dz);
        if (dist > 1e-6) {
            dx /= dist;
            dz /= dist;
        }
        if (e.style == EnemyStyle::Rusher) {
            if (dist > 1.8) {
                e.pos.x += dx * e.speed;
                e.pos.z += dz * e.speed;
            }
        } else if (e.style == EnemyStyle::Strafer) {
            if (tick % static_cast<std::uint64_t>(e.strafe_period) == 0U) e.strafe_dir = -e.strafe_dir;
            const double radial = std::max(-e.speed, std::min(e.speed, dist - 3.0));
            e.pos.x += dx * radial + (-dz) * e.strafe_dir * e.strafe_speed;
            e.pos.z += dz * radial + dx * e.strafe_dir * e.strafe_speed;
        }
        e.pos.y = player.pos.y;
        if (e.attack_cooldown > 0) --e.attack_cooldown;
        if (!player.dead && dist <= e.reach && e.attack_cooldown == 0) {
            hurt(e.damage, "enemy");
            e.attack_cooldown = e.attack_period;
            knock_ = Vec3{-dx * cfg_.knockback * 0.5, 0.0, -dz * cfg_.knockback * 0.5};
            knock_ticks_ = 2;
        }
    }
}

void SimWorld::step(const SimInput& in) {
    if (player.dead) {
        ++tick;
        return;
    }
    apply_actions(in);
    move_player(in);
    if (pearl_.active && --pearl_.ticks_left <= 0) {
        pearl_.active = false;
        const Vec3 t = pearl_.target;
        const bool body_free = !block_is_support(grid.get(fl(t.x), fl(t.y + 0.01), fl(t.z))) &&
                               !block_is_support(grid.get(fl(t.x), fl(t.y + 1.01), fl(t.z)));
        if (body_free) {
            player.pos = t;
            player.vy = 0.0;
            player.fall_distance = 0.0;
            player.on_ground = block_is_support(grid.get(fl(t.x), fl(t.y - 1e-6) , fl(t.z)));
            hurt(cfg_.pearl_damage, "pearl");
        }
    }
    if (player.attack_cooldown > 0) --player.attack_cooldown;
    if (in.attack && player.attack_cooldown == 0 && !player.dead) {
        const double yaw = in.yaw_deg * kPi / 180.0;
        const double fx = -std::sin(yaw);
        const double fz = std::cos(yaw);
        SimEnemy* target = nullptr;
        double best = 1e9;
        for (SimEnemy& e : enemies) {
            if (!e.alive()) continue;
            const double dx = e.pos.x - player.pos.x;
            const double dz = e.pos.z - player.pos.z;
            const double d = std::hypot(dx, dz);
            if (d > cfg_.attack_reach || d < 1e-6) continue;
            const double cosang = (dx * fx + dz * fz) / d;
            if (cosang < std::cos(35.0 * kPi / 180.0)) continue;
            if (d < best) {
                best = d;
                target = &e;
            }
        }
        if (target) {
            target->health -= cfg_.attack_damage;
            damage_dealt += cfg_.attack_damage;
            player.attack_cooldown = cfg_.attack_cooldown_ticks;
        }
    }
    move_enemies();
    ++tick;
}

} // namespace bedrockai
