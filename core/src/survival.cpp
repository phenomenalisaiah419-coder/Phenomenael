#include "core/survival.hpp"
namespace bedrockai {
SurvivalAssessment assess_survival(const WorldSnapshot& state, bool bed_broken){
    SurvivalAssessment a; a.critical_health=state.player.health<=4.0;
    a.void_danger=state.player.position.y < -64.0 || (!state.player.on_ground && state.player.velocity.y < -0.75 && state.player.position.y < 4.0);
    a.bed_broken=bed_broken;
    a.death_risk=a.critical_health||a.void_danger;
    a.emergency=a.death_risk || (a.bed_broken && state.player.health<10.0);
    return a;
}
}
