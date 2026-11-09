#include "core/state_reconciler.hpp"
#include <cmath>

namespace bedrockai {
static const PacketValue* pos_of(const PacketValue::Object& f){ auto it=f.find("position"); return it==f.end()?nullptr:&it->second; }
static const PacketValue* rot_of(const PacketValue::Object& f){ auto it=f.find("rotation"); return it==f.end()?nullptr:&it->second; }

bool StateReconciler::read_vec3(const PacketValue* v, Vec3& out){
    if(!v || v->object.empty()) return false;
    auto x=v->get("x"), y=v->get("y"), z=v->get("z");
    if(!x||!y||!z) return false;
    auto xv=x->as_number(), yv=y->as_number(), zv=z->as_number();
    if(!xv||!yv||!zv) return false;
    out={*xv,*yv,*zv}; return true;
}
bool StateReconciler::read_rot2(const PacketValue* v, Rot2& out){
    if(!v || v->object.empty()) return false;
    auto p=v->get("pitch"), y=v->get("yaw");
    if(!p||!y) return false;
    auto pv=p->as_number(), yv=y->as_number(); if(!pv||!yv) return false;
    out={*pv,*yv}; return true;
}
double StateReconciler::distance(const Vec3& a,const Vec3& b){ double x=a.x-b.x,y=a.y-b.y,z=a.z-b.z; return std::sqrt(x*x+y*y+z*z); }

bool StateReconciler::apply(const PacketView& packet, WorldSnapshot& state, std::string* error){
    if(packet.name=="start_game") return apply_start_game(packet,state,error);
    if(packet.name=="move_player") return apply_move_player(packet,state,error);
    if(packet.name=="level_chunk") return apply_level_chunk(packet,state,error);
    if(packet.name=="player_auth_input") return apply_auth_input(packet,state,error);
    state.tick=std::max(state.tick, packet.tick);
    return true;
}

bool StateReconciler::apply_start_game(const PacketView& p, WorldSnapshot& s, std::string* error){
    Vec3 pos{}; Rot2 rot{};
    if(!read_vec3(pos_of(p.fields),pos) || !read_rot2(rot_of(p.fields),rot)) { if(error)*error="start_game missing usable position/rotation"; return false; }
    s.player.position=pos; s.player.rotation=rot;
    if(auto it=p.fields.find("runtime_id"); it!=p.fields.end()) if(auto v=it->second.as_integer()) s.player.runtime_id=static_cast<std::uint64_t>(*v);
    s.player.last_server_tick=p.tick; s.tick=p.tick; s.joined=true; return true;
}

bool StateReconciler::apply_move_player(const PacketView& p, WorldSnapshot& s, std::string* error){
    Vec3 pos{}; Rot2 rot{};
    if(!read_vec3(pos_of(p.fields),pos)){ if(error)*error="move_player missing position"; return false; }
    s.last_correction.authoritative_position=pos;
    s.last_correction.distance_error=distance(s.player.position,pos);
    s.last_correction.present=s.last_correction.distance_error>0.001;
    if(read_rot2(rot_of(p.fields),rot)) { s.player.rotation=rot; s.last_correction.authoritative_rotation=rot; }
    s.player.position=pos; s.player.last_server_tick=p.tick; s.tick=std::max(s.tick,p.tick); return true;
}

bool StateReconciler::apply_level_chunk(const PacketView& p, WorldSnapshot& s, std::string* error){
    auto x=p.fields.find("x"), z=p.fields.find("z"), dim=p.fields.find("dimension_id");
    if(x==p.fields.end() || z==p.fields.end()){ if(error)*error="level_chunk missing x/z"; return false; }
    auto xv=x->second.as_integer(), zv=z->second.as_integer(); if(!xv||!zv){ if(error)*error="level_chunk x/z not integer"; return false; }
    if(dim!=p.fields.end()) if(auto d=dim->second.as_integer()) s.world.dimension=static_cast<int>(*d);
    s.tick=std::max(s.tick,p.tick); return true;
}

bool StateReconciler::apply_auth_input(const PacketView& p, WorldSnapshot& s, std::string* error){
    if(p.direction!=Direction::Serverbound) { if(error)*error="player_auth_input must be serverbound"; return false; }
    Vec3 pos{}; Rot2 rot{};
    if(read_vec3(pos_of(p.fields),pos)) s.player.position=pos;
    if(read_rot2(rot_of(p.fields),rot)) s.player.rotation=rot;
    if(auto it=p.fields.find("player_head_rotation"); it!=p.fields.end()) if(auto v=it->second.as_number()) s.player.head_yaw=*v;
    if(auto it=p.fields.find("client_tick"); it!=p.fields.end()) if(auto v=it->second.as_integer()) s.player.last_server_tick=static_cast<std::uint64_t>(*v);
    s.tick=std::max(s.tick,p.tick); return true;
}
}
