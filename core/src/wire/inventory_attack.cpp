#include "core/wire/inventory_attack.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bedrockai::wire {
namespace {
class Writer {
public:
    void u8(std::uint8_t v){b.push_back(v);} void u16(std::uint16_t v){b.push_back(static_cast<std::uint8_t>(v & 0xffu));b.push_back(static_cast<std::uint8_t>((v>>8)&0xffu));} void u64(std::uint64_t v){for(int i=0;i<8;++i){u8(static_cast<std::uint8_t>(v));v>>=8;}}
    void varu32(std::uint32_t v){while(v>=0x80u){u8(static_cast<std::uint8_t>(v)|0x80u);v>>=7;}u8(static_cast<std::uint8_t>(v));}
    void varu64(std::uint64_t v){while(v>=0x80u){u8(static_cast<std::uint8_t>(v)|0x80u);v>>=7;}u8(static_cast<std::uint8_t>(v));}
    void vari32(std::int32_t v){varu32(static_cast<std::uint32_t>((v<<1)^(v>>31)));}
    void i16(std::int16_t v){u8(static_cast<std::uint8_t>(v));u8(static_cast<std::uint8_t>(v>>8));}
    void f32(float v){std::uint32_t x;std::memcpy(&x,&v,4);for(int i=0;i<4;++i){u8(static_cast<std::uint8_t>(x));x>>=8;}}
    void vec(const Vec3& v){f32(static_cast<float>(v.x));f32(static_cast<float>(v.y));f32(static_cast<float>(v.z));}
    std::vector<std::uint8_t>b;
};
void empty_or_item(Writer&w,const AttackTransaction&a){
    const bool present=a.held_network_id!=0;
    w.i16(static_cast<std::int16_t>(a.held_network_id)); w.u16(a.held_count); w.varu32(a.held_metadata);
    const bool has_stack=a.held_stack_network_id!=0; w.u8(has_stack?1:0); if(has_stack)w.vari32(a.held_stack_network_id);
    w.varu32(0); // block runtime id
    if(!present){w.varu32(0);return;}
    // Minimal valid item user-data blob: empty NBT + no placement/break lists.
    Writer extra; extra.i16(-1); // NBT length sentinel
    extra.u8(0); // version (little-endian NBT) -- no compound payload emitted here
    // The empty NBT representation in this compact encoder is deliberately not
    // synthesized for non-empty items; reject those before reaching this path.
    w.varu32(static_cast<std::uint32_t>(extra.b.size()));
    w.b.insert(w.b.end(),extra.b.begin(),extra.b.end());
}
}
std::optional<std::vector<std::uint8_t>> InventoryAttackCodec::encode_attack_payload(const AttackTransaction&a,std::string*error){
    if(a.target_runtime_id==0){if(error)*error="attack target runtime id must be non-zero";return std::nullopt;}
    if(a.held_network_id!=0){if(error)*error="non-empty held items require the full item NBT codec and are not synthesized by this slice";return std::nullopt;}
    Writer w;
    w.vari32(0); // LegacyRequestID
    w.u8(0);      // has legacy slots
    w.varu32(kUseItemOnEntityTransaction);
    w.varu32(0); // transaction actions
    w.varu64(a.target_runtime_id);
    w.vari32(kAttackAction);
    w.vari32(a.hotbar_slot);
    empty_or_item(w,a);
    w.vec(a.position); w.vec(a.clicked_position);
    return w.b;
}
std::optional<std::vector<std::uint8_t>> InventoryAttackCodec::encode_attack_game_packet(const AttackTransaction&a,std::string*error){
    auto p=encode_attack_payload(a,error); if(!p)return std::nullopt; Writer w; w.varu32(kInventoryTransactionPacketId); w.b.insert(w.b.end(),p->begin(),p->end()); return w.b;
}
}
