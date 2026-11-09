#include "core/client_hook_dispatch.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace bedrockai;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"CHECK failed: %s at %d\n",#c,__LINE__); std::abort(); } } while(0)

struct Fake { std::uint64_t tick=0; std::uint64_t submits=0; EmbeddedActionFrame last{}; };
int rs(void* u, ClientStateView* o){auto*f=static_cast<Fake*>(u); if(!f||!o)return 0;*o={};o->tick=++f->tick;o->joined=true;o->player.health=20;return 1;}
int ri(void* u, ClientInventoryView* o){if(!u||!o)return 0;*o={};return 1;}
int rt(void* u, EmbeddedTouchInput* o){if(!u||!o)return 0;*o={};o->attack=true;return 1;}
void tf(void*){}
int sa(void* u,const EmbeddedActionFrame*a){auto*f=static_cast<Fake*>(u);if(!f||!a)return 0;f->last=*a;++f->submits;return 1;}

ClientAbiTable make(Fake& f){ClientAbiTable t;t.client_context=&f;t.tick=tf;t.read_state=rs;t.read_inventory=ri;t.read_touch=rt;t.submit_actions=sa;return t;}

void test_dispatch(){
 Fake f; ClientHookDispatcher d; auto t=make(f); std::string e; CHECK(d.attach(t,&e));
 EmbeddedClientControllerBridge bridge; ClientAbiTickRunner runner; EmbeddedActionFrame out;
 CHECK(d.on_tick(runner,bridge,[](const WorldSnapshot&,const InventoryState&,EmbeddedAiInput&a,std::string*){a={};a.propose_attack=true;return true;},out,&e));
 CHECK(f.submits==1); CHECK(f.last.attack);
 auto s=d.stats(); CHECK(s.ticks==1); CHECK(s.state_reads==1); CHECK(s.inventory_reads==1); CHECK(s.touch_reads==1); CHECK(s.action_submissions==1);
}

void test_direct_sites(){
 Fake f; ClientHookDispatcher d; auto t=make(f); std::string e; CHECK(d.attach(t,&e));
 ClientStateView st; ClientInventoryView inv; EmbeddedTouchInput touch; EmbeddedActionFrame action{};
 CHECK(d.on_read_state(st,&e)); CHECK(d.on_read_inventory(inv,&e)); CHECK(d.on_read_touch(touch,&e)); CHECK(d.on_submit_actions(action,&e));
 auto s=d.stats(); CHECK(s.state_reads==1); CHECK(s.inventory_reads==1); CHECK(s.touch_reads==1); CHECK(s.action_submissions==1);
 d.detach(); CHECK(!d.on_read_state(st,&e)); CHECK(s.rejected_calls==0); CHECK(d.stats().rejected_calls==1);
}

int main(){test_dispatch();test_direct_sites();return 0;}
