#pragma once

// Opponent modelling (v0.35): "prediction of enemy".
//
// Tracks each opponent's recent horizontal motion relative to the player, classifies the
// style (rusher / strafer / defensive / evasive / ranged), measures the strafe-reversal
// period, and predicts future position using that learned rhythm instead of plain linear
// extrapolation.  The archetype feeds SkillContext so that skill evidence transfers between
// similar opponents.  Bounded memory, deterministic, no network or file access.

#include "core/state.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace bedrockai {

enum class Archetype : std::uint8_t { Unknown, Rusher, Strafer, Defensive, Evasive, Ranged };
const char* archetype_name(Archetype a);

struct OpponentProfile {
    std::string id;
    Archetype archetype = Archetype::Unknown;
    double confidence = 0.0;
    int samples = 0;
    double mean_speed = 0.0;          // blocks per tick
    double mean_radial = 0.0;         // + toward the player
    double mean_lateral = 0.0;        // mean |lateral speed|
    double strafe_period_ticks = 0.0; // 0 = no rhythm detected
    double ranged_fraction = 0.0;
};

struct OpponentModelConfig {
    std::size_t history = 64;
    std::size_t max_opponents = 64;
    int min_samples = 16;
    double lateral_deadband = 0.02;   // blocks/tick below which lateral motion has no sign
};

class OpponentModel {
public:
    explicit OpponentModel(OpponentModelConfig config = {});
    void observe(const std::string& id, std::uint64_t tick, const Vec3& opponent, const Vec3& player,
                 bool used_ranged = false);
    OpponentProfile profile(const std::string& id) const;
    // Learned prediction `ticks_ahead` ticks after the latest sample (horizontal rhythm, y copied).
    Vec3 predict_position(const std::string& id, int ticks_ahead) const;
    // Naive constant-velocity prediction (baseline used to prove the learned one is better).
    Vec3 predict_linear(const std::string& id, int ticks_ahead) const;
    std::size_t tracked() const { return tracks_.size(); }
    void forget(const std::string& id) { tracks_.erase(id); }

private:
    struct Sample {
        std::uint64_t tick = 0;
        Vec3 opp{};
        Vec3 me{};
    };
    struct Track {
        std::deque<Sample> samples;
        std::uint64_t ranged = 0;
        std::uint64_t total = 0;
        std::uint64_t last_seen = 0;
    };
    OpponentModelConfig cfg_;
    std::unordered_map<std::string, Track> tracks_;

    struct Stats {
        double speed = 0.0, radial = 0.0, lateral_abs = 0.0, period = 0.0;
        std::uint64_t last_flip_tick = 0;
        bool has_flip = false;
        double v_radial = 0.0, v_lateral = 0.0;  // smoothed latest velocity components
        double nx = 0.0, nz = 0.0;               // unit vector opponent -> player at latest sample
        int flips = 0;
        bool ok = false;
    };
    Stats analyse(const Track& t) const;
};

} // namespace bedrockai
