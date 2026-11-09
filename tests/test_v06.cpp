#include "core/wire/player_auth_input.hpp"
#include "core/wire/inventory_attack.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace bedrockai;

static void test_auth_roundtrip(){
    wire::PlayerAuthInputPacket p;
    p.pitch=10.5f; p.yaw=20.25f; p.position={1.375,64,-2.5}; p.move_x=-1; p.move_z=1; p.head_yaw=30.75f;
    p.input_flags={6,10,12,20}; p.input_mode=1; p.play_mode=0; p.interaction_model=1; p.interact_pitch=10.5f; p.interact_yaw=20.25f;
    p.tick=1234; p.delta={.25,0,-.5}; p.analogue_move={-1,1}; p.camera_orientation={.25,-.5,-.75}; p.raw_move={-1,1};
    wire::WireError e; auto enc=wire::PlayerAuthInputCodec::encode_payload(p,&e); assert(enc && e.message.empty());
    // The protocol-2193 Cinnabar witness is 99 bytes including its 4-byte
    // game-packet envelope; our payload starts after that envelope.
    assert(enc->size()==95);
    const std::vector<std::uint8_t> expected_wire = {0x00,0x00,0x28,0x41,0x00,0x00,0xa2,0x41,0x00,0x00,0xb0,0x3f,0x00,0x00,0x80,0x42,0x00,0x00,0x20,0xc0,0x00,0x00,0x80,0xbf,0x00,0x00,0x80,0x3f,0x00,0x00,0xf6,0x41,0x04,0x0c,0x14,0x18,0x28,0x01,0x00,0x02,0x00,0x00,0x28,0x41,0x00,0x00,0xa2,0x41,0xd2,0x09,0x00,0x00,0x80,0x3e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbf,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xbf,0x00,0x00,0x80,0x3f,0x00,0x00,0x80,0x3e,0x00,0x00,0x00,0xbf,0x00,0x00,0x40,0xbf,0x00,0x00,0x80,0xbf,0x00,0x00,0x80,0x3f};
    assert(*enc == expected_wire);
    auto dec=wire::PlayerAuthInputCodec::decode_payload(*enc,&e); assert(dec && e.message.empty()); assert(std::fabs(dec->position.x-1.375)<1e-6); assert(dec->input_flags.size()==4 && dec->input_flags[1]==10); assert(dec->tick==1234);
}

static void test_auth_rejects_optionals(){ wire::PlayerAuthInputPacket p; p.block_actions_present=true; wire::WireError e; auto x=wire::PlayerAuthInputCodec::encode_payload(p,&e); assert(!x && e.message.find("optional")!=std::string::npos); }
static void test_auth_rejects_duplicate_flags(){ wire::PlayerAuthInputPacket p; p.input_flags={10,10}; wire::WireError e; auto x=wire::PlayerAuthInputCodec::encode_payload(p,&e); assert(!x); }
static void test_auth_game_packet_id(){ wire::PlayerAuthInputPacket p; wire::WireError e; auto x=wire::PlayerAuthInputCodec::encode_game_packet(p,&e); assert(x && x->at(0)==144 && x->at(1)==1); }
static void test_attack_empty_hand(){ wire::AttackTransaction a; a.target_runtime_id=~std::uint64_t(0); a.hotbar_slot=0; a.position={-12.5,70,31.75}; a.clicked_position={-.5,.625,1.5}; std::string e; auto x=wire::InventoryAttackCodec::encode_attack_payload(a,&e); assert(x); assert(x->size()==48); const std::vector<std::uint8_t> expected = {0x00,0x00,0x03,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x01,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x48,0xc1,0x00,0x00,0x8c,0x42,0x00,0x00,0xfe,0x41,0x00,0x00,0x00,0xbf,0x00,0x00,0x20,0x3f,0x00,0x00,0xc0,0x3f}; if(*x!=expected){ std::cerr<<"got="; for(auto b:*x) std::cerr<<std::hex<<((int)b<16?"0":"")<<(int)b; std::cerr<<"\nexp="; for(auto b:expected) std::cerr<<std::hex<<((int)b<16?"0":"")<<(int)b; std::cerr<<"\n";} assert(*x==expected); auto g=wire::InventoryAttackCodec::encode_attack_game_packet(a,&e); assert(g && g->at(0)==30); }
static void test_attack_requires_target(){ wire::AttackTransaction a{}; std::string e; const auto encoded = wire::InventoryAttackCodec::encode_attack_payload(a,&e); (void)encoded; assert(!encoded); }

int main(){test_auth_roundtrip();test_auth_rejects_optionals();test_auth_rejects_duplicate_flags();test_auth_game_packet_id();test_attack_empty_hand();test_attack_requires_target();std::cout<<"v0.6 wire tests: PASS\n";}
