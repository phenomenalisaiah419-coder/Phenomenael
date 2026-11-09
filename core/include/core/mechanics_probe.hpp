#pragma once

#include "state.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace bedrockai {

enum class ProbeKind { AttackDistance, AttackCadence, PlacementReach, Knockback, Pearl, WindCharge, Fireball, Bridge, EdgeRecovery };

struct ProbeObservation {
    ProbeKind kind;
    double value = 0;
    std::uint64_t tick = 0;
    bool success = false;
    std::string note;
};

struct ProbeStatistics {
    std::size_t count = 0;
    std::size_t successes = 0;
    double min = 0;
    double max = 0;
    double mean = 0;
};

class MechanicsProbe {
public:
    void record(const ProbeObservation& observation);
    ProbeStatistics summarize(ProbeKind kind) const;
    std::string export_json() const;
    void clear();

private:
    std::vector<ProbeObservation> observations_;
};

} // namespace bedrockai
