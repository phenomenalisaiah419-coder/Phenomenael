#include "core/wire/player_auth_input.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace bedrockai::wire {
namespace {

class Writer {
public:
    void u8(std::uint8_t v) { b.push_back(v); }
    void u16(float v) { float_le(v); }
    void f32(float v) { float_le(v); }
    void u32(std::uint32_t v) { for(int i=0;i<4;++i){ b.push_back(static_cast<std::uint8_t>(v&0xffu)); v>>=8; } }
    void u64(std::uint64_t v) { for(int i=0;i<8;++i){ b.push_back(static_cast<std::uint8_t>(v&0xffu)); v>>=8; } }
    void varu32(std::uint32_t v) { while(v>=0x80u){u8(static_cast<std::uint8_t>(v)|0x80u);v>>=7;}u8(static_cast<std::uint8_t>(v)); }
    void varu64(std::uint64_t v) { while(v>=0x80u){u8(static_cast<std::uint8_t>(v)|0x80u);v>>=7;}u8(static_cast<std::uint8_t>(v)); }
    void vari32(std::int32_t v) { varu32(static_cast<std::uint32_t>((v<<1) ^ (v>>31))); }
    void bytes(const std::vector<std::uint8_t>& x) { b.insert(b.end(),x.begin(),x.end()); }
    std::vector<std::uint8_t> b;
private:
    void float_le(float v) { static_assert(sizeof(float)==4); std::uint32_t u=0; std::memcpy(&u,&v,4); u32(u); }
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes):b(bytes){}
    bool u8(std::uint8_t& v){return take(1,[&]{v=b[p];});}
    bool u32(std::uint32_t& v){ if(p+4>b.size()) return false; v=0; for(int i=0;i<4;++i)v|=std::uint32_t(b[p++])<<(8*i); return true; }
    bool f32(float& v){std::uint32_t u; if(!u32(u))return false; std::memcpy(&v,&u,4); return std::isfinite(v);}
    bool varu32(std::uint32_t& out){std::uint32_t r=0; for(unsigned i=0;i<5;++i){std::uint8_t x; if(!u8(x))return false; if(i==4 && x>1)return false; r |= std::uint32_t(x&0x7fu)<<(7*i); if(!(x&0x80u)){out=r;return true;}}return false;}
    bool varu64(std::uint64_t& out){std::uint64_t r=0; for(unsigned i=0;i<10;++i){std::uint8_t x; if(!u8(x))return false; if(i==9 && x>1)return false; r |= std::uint64_t(x&0x7fu)<<(7*i); if(!(x&0x80u)){out=r;return true;}}return false;}
    bool vari32(std::int32_t& out){std::uint32_t u; if(!varu32(u))return false; out=static_cast<std::int32_t>((u>>1)^static_cast<std::uint32_t>(-static_cast<std::int32_t>(u&1))); return true;}
    std::size_t pos()const{return p;}
    bool end()const{return p==b.size();}
private:
    template<class F> bool take(std::size_t n,F f){if(p+n>b.size())return false;f();++p;return true;}
    const std::vector<std::uint8_t>& b; std::size_t p=0;
};

void err(WireError* e,const std::string& msg,std::size_t at){if(e){e->message=msg;e->offset=at;}}
void f3(Writer& w,const Vec3& v){w.f32(static_cast<float>(v.x));w.f32(static_cast<float>(v.y));w.f32(static_cast<float>(v.z));}
bool f3(Reader& r,Vec3& v){float x,y,z; if(!r.f32(x)||!r.f32(y)||!r.f32(z))return false; v={x,y,z};return true;}
float clamp_pitch(float v){return std::clamp(v,-90.0f,90.0f);}
float wrap_yaw(float v){ while(v>180.f)v-=360.f; while(v<=-180.f)v+=360.f; return v; }
void push_unique(std::vector<std::int32_t>& ids,std::int32_t id){if(std::find(ids.begin(),ids.end(),id)==ids.end())ids.push_back(id);}

} // namespace

std::optional<std::vector<std::uint8_t>> PlayerAuthInputCodec::encode_payload(
    const PlayerAuthInputPacket& p, WireError* e) {
    if(p.input_flags.size()>kInputFlagCount){err(e,"too many PlayerAuthInput flags",0);return std::nullopt;}
    std::vector<std::int32_t> flags=p.input_flags;
    std::sort(flags.begin(),flags.end());
    if(std::adjacent_find(flags.begin(),flags.end())!=flags.end()){err(e,"duplicate PlayerAuthInput flag",0);return std::nullopt;}
    for(auto id:flags) if(id<0 || static_cast<std::size_t>(id)>=kInputFlagCount){err(e,"PlayerAuthInput flag out of range",0);return std::nullopt;}
    if(p.item_interaction_present||p.item_stack_request_present||p.block_actions_present||p.vehicle_rotation_present||p.client_predicted_vehicle_present){err(e,"optional PlayerAuthInput payloads are not encoded by this slice",0);return std::nullopt;}
    Writer w;
    w.f32(p.pitch); w.f32(p.yaw); f3(w,p.position);
    w.f32(p.move_x); w.f32(p.move_z); w.f32(p.head_yaw);
    w.varu32(static_cast<std::uint32_t>(flags.size())); for(auto id:flags)w.vari32(id);
    w.varu32(p.input_mode); w.varu32(p.play_mode); w.vari32(p.interaction_model);
    w.f32(p.interact_pitch); w.f32(p.interact_yaw); w.varu64(p.tick); f3(w,p.delta);
    // Protocol 2193 carries one presence byte for each of these optionals.
    w.u8(0); w.u8(0); w.u8(0); w.u8(0); w.u8(0);
    w.f32(p.analogue_move.x); w.f32(p.analogue_move.y);
    w.f32(static_cast<float>(p.camera_orientation.x)); w.f32(static_cast<float>(p.camera_orientation.y)); w.f32(static_cast<float>(p.camera_orientation.z));
    w.f32(p.raw_move.x); w.f32(p.raw_move.y);
    return w.b;
}

std::optional<PlayerAuthInputPacket> PlayerAuthInputCodec::decode_payload(
    const std::vector<std::uint8_t>& bytes, WireError* e) {
    Reader r(bytes); PlayerAuthInputPacket p; float f1,f2;
    if(!r.f32(f1)||!r.f32(f2)||!f3(r,p.position)){err(e,"truncated PlayerAuthInput header",r.pos());return std::nullopt;}
    p.pitch=f1; p.yaw=f2;
    if(!r.f32(p.move_x)||!r.f32(p.move_z)||!r.f32(p.head_yaw)){err(e,"truncated PlayerAuthInput movement fields",r.pos());return std::nullopt;}
    std::uint32_t count=0; if(!r.varu32(count)||count>kInputFlagCount){err(e,"invalid PlayerAuthInput flag count",r.pos());return std::nullopt;}
    p.input_flags.reserve(count); for(std::uint32_t i=0;i<count;++i){std::int32_t id; if(!r.vari32(id)||id<0||static_cast<std::size_t>(id)>=kInputFlagCount){err(e,"invalid PlayerAuthInput flag",r.pos());return std::nullopt;} if(std::find(p.input_flags.begin(),p.input_flags.end(),id)!=p.input_flags.end()){err(e,"duplicate PlayerAuthInput flag",r.pos());return std::nullopt;} p.input_flags.push_back(id);}
    if(!r.varu32(p.input_mode)||!r.varu32(p.play_mode)||!r.vari32(p.interaction_model)){err(e,"truncated PlayerAuthInput mode fields",r.pos());return std::nullopt;}
    if(!r.f32(p.interact_pitch)||!r.f32(p.interact_yaw)||!r.varu64(p.tick)||!f3(r,p.delta)){err(e,"truncated PlayerAuthInput timing fields",r.pos());return std::nullopt;}
    std::uint8_t present=0;
    if(!r.u8(present)){err(e,"missing PlayerAuthInput item-interaction presence",r.pos());return std::nullopt;} p.item_interaction_present=present!=0;
    if(!r.u8(present)){err(e,"missing PlayerAuthInput item-stack presence",r.pos());return std::nullopt;} p.item_stack_request_present=present!=0;
    if(!r.u8(present)){err(e,"missing PlayerAuthInput block-actions presence",r.pos());return std::nullopt;} p.block_actions_present=present!=0;
    if(!r.u8(present)){err(e,"missing PlayerAuthInput vehicle-rotation presence",r.pos());return std::nullopt;} p.vehicle_rotation_present=present!=0;
    if(!r.u8(present)){err(e,"missing PlayerAuthInput vehicle-id presence",r.pos());return std::nullopt;} p.client_predicted_vehicle_present=present!=0;
    if(p.item_interaction_present||p.item_stack_request_present||p.block_actions_present||p.vehicle_rotation_present||p.client_predicted_vehicle_present){err(e,"unsupported optional PlayerAuthInput payload encountered",r.pos()-5);return std::nullopt;}
    if(!r.f32(p.analogue_move.x)||!r.f32(p.analogue_move.y)||!f3(r,p.camera_orientation)||!r.f32(p.raw_move.x)||!r.f32(p.raw_move.y)){err(e,"truncated PlayerAuthInput trailing fields",r.pos());return std::nullopt;}
    if(!r.end()){err(e,"trailing bytes after PlayerAuthInput",r.pos());return std::nullopt;}
    return p;
}

std::optional<std::vector<std::uint8_t>> PlayerAuthInputCodec::encode_game_packet(
    const PlayerAuthInputPacket& p, WireError* e) {
    auto payload=encode_payload(p,e); if(!payload)return std::nullopt;
    Writer w; w.varu32(kPlayerAuthInputPacketId); w.bytes(*payload); return w.b;
}

PlayerAuthInputPacket make_player_auth_input(
    const WorldSnapshot& snapshot,const MovementCommand& movement,const AimCommand& aim,
    bool human_attack_held,bool human_place_held) {
    (void)human_attack_held; (void)human_place_held;
    PlayerAuthInputPacket p;
    p.pitch=clamp_pitch(static_cast<float>(snapshot.player.rotation.pitch + aim.pitch_delta));
    p.yaw=wrap_yaw(static_cast<float>(snapshot.player.rotation.yaw + aim.yaw_delta));
    p.position=snapshot.player.position;
    p.move_x=static_cast<float>(movement.strafe);
    p.move_z=static_cast<float>(movement.forward);
    p.head_yaw=p.yaw;
    p.input_mode=2;
    p.play_mode=0;
    p.interaction_model=1; // crosshair
    p.interact_pitch=p.pitch; p.interact_yaw=p.yaw;
    p.tick=snapshot.tick+1;
    p.delta=snapshot.player.velocity;
    p.analogue_move={static_cast<float>(movement.strafe),static_cast<float>(movement.forward)};
    const double pr=p.pitch*3.141592653589793/180.0, yr=p.yaw*3.141592653589793/180.0;
    p.camera_orientation={-static_cast<double>(std::sin(yr)*std::cos(pr)), -static_cast<double>(std::sin(pr)), static_cast<double>(std::cos(yr)*std::cos(pr))};
    p.raw_move={static_cast<float>(movement.strafe),static_cast<float>(movement.forward)};
    if(movement.forward>0.05) push_unique(p.input_flags,10);
    if(movement.forward<-0.05) push_unique(p.input_flags,11);
    if(movement.strafe<-0.05) push_unique(p.input_flags,12);
    if(movement.strafe>0.05) push_unique(p.input_flags,13);
    if(movement.jump) push_unique(p.input_flags,6);
    if(movement.sneak) push_unique(p.input_flags,8);
    if(movement.sprint) push_unique(p.input_flags,20);
    return p;
}

} // namespace bedrockai::wire
