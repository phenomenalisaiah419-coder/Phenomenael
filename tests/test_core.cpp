#include "core/arbiter.hpp"
#include "core/human_intent.hpp"
#include "core/mechanics_probe.hpp"
#include "core/movement_controller.hpp"
#include "core/aim_controller.hpp"
#include "core/combat_controller.hpp"
#include "core/shared_control.hpp"
#include "core/protocol_adapter.hpp"
#include "core/state_reconciler.hpp"
#include "core/transport.hpp"
#include <cassert>
#include <iostream>
#include <string>
using namespace bedrockai;

#define CHECK(cond) do { if(!(cond)) { std::cerr << "FAIL: " << #cond << "\n"; return 1; } } while(false)

static PacketValue vec3(double x,double y,double z){return PacketValue::object_value({{"x",PacketValue::number(x)},{"y",PacketValue::number(y)},{"z",PacketValue::number(z)}});}
static PacketValue rot(double p,double y){return PacketValue::object_value({{"pitch",PacketValue::number(p)},{"yaw",PacketValue::number(y)}});}

int main(){
    CapabilityProfile caps;
    HumanInput human{};
    AiProposal ai{}; ai.mode=ControlMode::Takeover;
    ai.attack=Action{ActionKind::Attack};
    auto f=arbitrate(caps,{},human,ai); CHECK(!f.attack);
    human.touch.attack_held=true; f=arbitrate(caps,{},human,ai); CHECK(f.attack);
    human.touch.attack_held=false; ai.attack.reset(); ai.place=Action{ActionKind::PlaceBlock,false}; f=arbitrate(caps,{},human,ai); CHECK(!f.place);
    ai.emergency=true; ai.place->emergency_allowed=true; f=arbitrate(caps,{},human,ai); CHECK(f.place);
    human.touch.explicit_override=true; f=arbitrate(caps,{},human,ai); CHECK(!f.movement&&!f.place);

    ProtocolAdapter adapter; std::string err;
    CHECK(!adapter.accept_server_identity({"1.26.50",2192,BedrockTransport::RakNet},&err));
    CHECK(adapter.accept_server_identity({"1.26.50",2193,BedrockTransport::RakNet},&err));
    PacketView start{Direction::Clientbound,packet_id::StartGame,"start_game",{{"runtime_id",PacketValue::integer(42)},{"position",vec3(1,2,3)},{"rotation",rot(10,90)}},1};
    CHECK(adapter.observe(start,&err));
    WorldSnapshot state; StateReconciler reconciler; CHECK(reconciler.apply(start,state,&err)); CHECK(state.joined&&state.player.runtime_id==42&&state.player.position.x==1);
    PacketView auth{Direction::Serverbound,packet_id::PlayerAuthInput,"player_auth_input",{{"position",vec3(2,2,3)},{"rotation",rot(11,91)},{"client_tick",PacketValue::integer(12)}},12};
    CHECK(adapter.observe(auth,&err)); CHECK(reconciler.apply(auth,state,&err)); CHECK(state.tick==12&&state.player.position.x==2);
    PacketView bad{Direction::Clientbound,packet_id::MovePlayer,"not_move_player",{},13}; CHECK(!adapter.observe(bad,&err));
    PacketView wrong_direction{Direction::Serverbound,packet_id::MovePlayer,"move_player",{},14}; CHECK(!adapter.observe(wrong_direction,&err));

    HumanIntentMapper mapper; TouchFrame touch; touch.attack=true; touch.move_x=.75; touch.look_dy=-2; auto intent=mapper.map(touch); CHECK(intent.attack_held&&intent.move_x==.75&&intent.look_dy==-2);

    MechanicsProbe probe; probe.record({ProbeKind::AttackDistance,3.1,1,true,"hit"}); probe.record({ProbeKind::AttackDistance,3.4,2,true,"hit"}); auto stat=probe.summarize(ProbeKind::AttackDistance); CHECK(stat.count==2&&stat.successes==2&&stat.mean>3.2&&stat.mean<3.3); CHECK(probe.export_json().find("2193")!=std::string::npos);

    WorldSnapshot movement_state;
    movement_state.player.position = {0, 0, 0};
    movement_state.player.rotation = {0, 0};
    movement_state.player.on_ground = true;
    MovementController mover;
    auto move = mover.compute(movement_state, MovementTarget{{0, 0, 8}, MovementStyle::Approach, true, true, false, 2.5, 1});
    CHECK(move.valid && move.forward > 0.9 && std::abs(move.strafe) < 0.1);
    move = mover.compute(movement_state, MovementTarget{{0, 1, 8}, MovementStyle::Approach, true, false, false, 2.5, 1});
    CHECK(move.jump);

    AimController aimer;
    auto aim = aimer.compute(movement_state, AimTarget{{0, 0, 8}, {}, false, 0});
    CHECK(aim.valid && std::abs(aim.desired_yaw) < 0.001 && std::abs(aim.desired_pitch - 7.95) < 0.5);
    movement_state.player.rotation.yaw = 179;
    aim = aimer.compute(movement_state, AimTarget{{0, 0, -8}, {}, false, 0});
    CHECK(aim.valid && std::abs(aim.yaw_delta) < 10.0);

    movement_state.world.entities.clear();
    movement_state.world.entities.emplace(7, EntityState{7, 1, {0, 0, 3}, {}, 20, true, true});
    CombatController unmeasured;
    auto combat = unmeasured.decide(movement_state, CombatStyle::Aggressive);
    CHECK(combat.valid && combat.target_runtime_id.has_value() && !combat.propose_attack);
    CombatController measured(CombatConfig{4.0, 0.5, 0.35, 6.0, -8.0, -64.0});
    combat = measured.decide(movement_state, CombatStyle::Aggressive);
    CHECK(combat.propose_attack && combat.phase == CombatPhase::Engage);
    movement_state.player.health = 5.0;
    movement_state.player.rotation.yaw = 0.0;
    combat = measured.decide(movement_state, CombatStyle::Aggressive);
    CHECK(!combat.propose_attack && combat.movement.forward < 0.0);

    SharedControlController shared;
    SharedControlInput shared_input;
    shared_input.ai = combat;
    shared_input.ai.emergency = false;
    shared_input.human.touch.attack_held = true;
    shared_input.human.touch.move_x = 0.0;
    shared_input.human.touch.move_y = 0.0;
    auto shared_out = shared.compose(movement_state, {}, shared_input, CapabilityProfile{});
    CHECK(shared_out.channels.attack == false || !combat.propose_attack);

    movement_state.player.health = 20.0;
    shared_input.ai = measured.decide(movement_state, CombatStyle::Aggressive);
    shared_input.human.touch.attack_held = true;
    shared_out = shared.compose(movement_state, {}, shared_input, CapabilityProfile{});
    CHECK(shared_out.channels.attack && shared_out.movement.valid && shared_out.aim.valid);

    shared_input.human.touch.move_x = -1.0;
    shared_input.human.touch.move_y = 0.0;
    shared_out = shared.compose(movement_state, {}, shared_input, CapabilityProfile{});
    CHECK(shared_out.movement.strafe == -1.0);

    std::cout<<"core tests: PASS\n";
}
