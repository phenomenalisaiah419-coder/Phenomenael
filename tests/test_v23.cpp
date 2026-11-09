#include "core/bedrock_client_integration.hpp"
#include "core/hook_plan.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

using namespace bedrockai;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"CHECK failed: %s at %d\n",#c,__LINE__); std::abort(); } } while(0)

struct Harness {
    ClientStateView state{};
    ClientInventoryView inventory{};
    EmbeddedTouchInput touch{};
    EmbeddedActionFrame last{};
    ClientHookDispatchRegistration dispatch{};
    std::vector<std::uint8_t> image = std::vector<std::uint8_t>(64, 0);
    std::uint64_t submits = 0;
};

int rs(void* u, ClientStateView* o) { auto*h=static_cast<Harness*>(u); if(!h||!o)return 0; *o=h->state; return 1; }
int ri(void* u, ClientInventoryView* o) { auto*h=static_cast<Harness*>(u); if(!h||!o)return 0; *o=h->inventory; return 1; }
int rt(void* u, EmbeddedTouchInput* o) { auto*h=static_cast<Harness*>(u); if(!h||!o)return 0; *o=h->touch; return 1; }
std::atomic<bool> ai_entered{false};
int ai(void*, const WorldSnapshot*, const InventoryState*, EmbeddedAiInput* o) { if(!o)return 0; ai_entered.store(true,std::memory_order_release); std::this_thread::sleep_for(std::chrono::milliseconds(20)); *o={}; o->propose_attack=true; return 1; }
int submit(void* u, const EmbeddedActionFrame* a) { auto*h=static_cast<Harness*>(u); if(!h||!a)return 0; h->last=*a; ++h->submits; return 1; }
void tick_identity(void*) {}
int image(void* u, ClientImageInfo* o) { auto*h=static_cast<Harness*>(u); if(!h||!o)return 0; *o={}; o->base=reinterpret_cast<std::uintptr_t>(h->image.data()); o->mapped_size=h->image.size(); o->library_name=kEmbeddedClientLibrary; o->build_id_hex="aabbccdd"; o->elf_class=2; o->machine=183; o->executable_segments.push_back({o->base,o->mapped_size,5}); return 1; }
int install(void*, const ClientHookBinding* b, std::size_t n) { return b && n==5 ? 1 : 0; }

int native_cert(void* u, ClientNativeAbiCertificate* o, std::string* e) {
    auto* h=static_cast<Harness*>(u);
    if(!h||!o){if(e)*e="bad native certificate request";return 0;}
    *o=ClientNativeAbiCertificate{};
    o->build_id_hex="aabbccdd";
    for(std::size_t i=0;i<kClientHookKindCount;++i){
        o->hook_target_addresses[i]=reinterpret_cast<std::uintptr_t>(h->image.data()) + i*4U;
        o->translator_addresses[i]=reinterpret_cast<std::uintptr_t>(h->image.data()) + 40U + i;
    }
    return 1;
}

int register_dispatch(void* u, const ClientHookDispatchRegistration* r, std::string* e) { auto*h=static_cast<Harness*>(u); if(!h||!r||r->abi_version!=kClientHookDispatchAbiVersion||!r->on_tick||!r->on_read_state||!r->on_read_inventory||!r->on_read_touch||!r->on_submit_actions){if(e)*e="bad registration";return 0;} h->dispatch=*r; return 1; }

std::string plan() {
    return
        "Tick|tick|aabbccdd|AA 01 02 03|AA 01 02 03|0|4\n"
        "ReadState|state|aabbccdd|BB 04 05 06|BB 04 05 06|0|4\n"
        "ReadInventory|inventory|aabbccdd|CC 07 08 09|CC 07 08 09|0|4\n"
        "ReadTouch|touch|aabbccdd|DD 0A 0B 0C|DD 0A 0B 0C|0|4\n"
        "SubmitActions|actions|aabbccdd|EE 0D 0E 0F|EE 0D 0E 0F|0|4\n";
}

int main() {
    Harness h; for (std::size_t i=0;i<h.image.size();++i) h.image[i]=static_cast<std::uint8_t>(i+1); h.image[0]=0xAA; h.image[1]=0x01; h.image[2]=0x02; h.image[3]=0x03; h.image[4]=0xBB; h.image[5]=0x04; h.image[6]=0x05; h.image[7]=0x06; h.image[8]=0xCC; h.image[9]=0x07; h.image[10]=0x08; h.image[11]=0x09; h.image[12]=0xDD; h.image[13]=0x0A; h.image[14]=0x0B; h.image[15]=0x0C; h.image[16]=0xEE; h.image[17]=0x0D; h.image[18]=0x0E; h.image[19]=0x0F; h.state.tick=42; h.state.joined=true; h.state.player.health=20; h.touch.attack=true;
    EmbeddedClientHostApi api; api.abi_version=kEmbeddedClientAbiVersion; api.protocol=kEmbeddedClientProtocol;
    api.game_version=kEmbeddedClientGameVersion; api.library_name=kEmbeddedClientLibrary; api.user=&h;
    api.read_client_state=rs; api.read_inventory=ri; api.read_touch=rt; api.compute_ai=ai; api.submit_actions=submit;
    api.tick_identity=tick_identity; api.get_image_info=image; api.install_client_hooks=install; api.register_hook_dispatch=register_dispatch; api.get_native_abi_certificate=native_cert;
    EmbeddedBedrockClientIntegration runtime; std::string e; CHECK(runtime.attach(api,&e));
    std::vector<ClientHookSpec> specs; CHECK(parse_client_hook_plan(plan(),specs,&e));
    if (!runtime.install_complete_hook_plan(specs,&e)) { std::fprintf(stderr,"ERR:%s\n",e.c_str()); return 1; } CHECK(runtime.hook_dispatch_registered());
    ClientHookEventMetadata event; event.tick = h.state.tick;
    CHECK(h.dispatch.on_read_state(h.dispatch.user,&event,&h.state)==1);
    CHECK(h.dispatch.on_read_inventory(h.dispatch.user,&event,&h.inventory)==1);
    CHECK(h.dispatch.on_read_touch(h.dispatch.user,&event,&h.touch)==1);
    CHECK(h.dispatch.on_tick(h.dispatch.user,&event)==1); CHECK(h.submits==1); CHECK(h.last.attack); CHECK(runtime.tick_count()==1);
    CHECK(h.dispatch.on_submit_actions(h.dispatch.user,&event,&h.last)==1);
    h.state.player.health=-1; event.tick = h.state.tick; CHECK(h.dispatch.on_read_state(h.dispatch.user,&event,&h.state)==0);
    h.state.player.health=20;
    h.state.tick = 43U; event.tick = h.state.tick; CHECK(h.dispatch.on_read_state(h.dispatch.user,&event,&h.state)==1); CHECK(h.dispatch.on_read_inventory(h.dispatch.user,&event,&h.inventory)==1); CHECK(h.dispatch.on_read_touch(h.dispatch.user,&event,&h.touch)==1);
    ai_entered.store(false,std::memory_order_release);
    std::thread live_tick([&] { (void)h.dispatch.on_tick(h.dispatch.user,&event); });
    for (int i=0; i<100 && !ai_entered.load(std::memory_order_acquire); ++i) std::this_thread::yield();
    runtime.detach();
    live_tick.join();
    CHECK(!runtime.hook_dispatch_registered());
    return 0;
}
