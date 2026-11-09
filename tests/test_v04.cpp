#include "core/bridge_controller.hpp"
#include "core/utility_controller.hpp"
#include "core/survival_controller.hpp"
#include "core/arbiter.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace bedrockai;
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL: "<<#x<<"\n";return 1;}}while(0)
int main(){
    WorldSnapshot s; s.joined=true; s.player.position={0,10,0}; s.player.rotation={0,0}; s.player.on_ground=true;
    BridgeController bridge;
    auto b=bridge.compute(s,{0,10,5},BridgeMode::Normal,false,false);
    CHECK(b.valid && !b.placement.has_value());
    b=bridge.compute(s,{0,10,5},BridgeMode::Normal,true,false);
    CHECK(b.valid && b.requires_place_hold);

    UtilityInventory inv; inv.pearl=true; inv.wind_charge=true; inv.fireball=true;
    s.player.airborne=true; s.player.velocity.y=-8;
    auto u=UtilityController{}.decide(s,inv,UtilityTrigger::None);
    CHECK(u.valid && u.kind==UtilityKind::WindCharge && u.emergency);

    s.player.airborne=false; s.player.position.y=-60; s.player.velocity={0,0,0};
    auto sv=SurvivalController{}.decide(s,inv);
    CHECK(sv.valid && sv.phase==SurvivalPhase::VoidRecovery && sv.emergency_place.has_value());

    CapabilityProfile caps;
    HumanInput h{}; AiProposal ai; ai.mode=ControlMode::Emergency; ai.emergency=true;
    ai.place=Action{ActionKind::PlaceBlock,true};
    auto c=arbitrate(caps,{},h,ai); CHECK(c.place);
    h.touch.explicit_override=true; c=arbitrate(caps,{},h,ai); CHECK(!c.place && !c.movement);

    std::cout<<"v0.4 tests: PASS\n";
}
