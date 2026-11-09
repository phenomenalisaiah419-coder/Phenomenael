#pragma once
#include "state.hpp"
namespace bedrockai {
struct SurvivalAssessment { bool critical_health=false; bool void_danger=false; bool bed_broken=false; bool death_risk=false; bool emergency=false; };
SurvivalAssessment assess_survival(const WorldSnapshot& state, bool bed_broken);
}
