#include "core/bedrock_client_integration.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

using namespace bedrockai;

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "CHECK failed: %s at %d\n", #condition, __LINE__); std::abort(); } } while (0)

struct Harness {
    ClientStateView state;
    ClientInventoryView inventory;
    EmbeddedTouchInput touch;
    EmbeddedActionFrame last{};
    bool registered = false;
    void (*tick_cb)(void*) = nullptr;
    void* tick_user = nullptr;
    ClientImageInfo image{};
};

int read_state(void* user, ClientStateView* out) { *out = static_cast<Harness*>(user)->state; return 1; }
int read_inventory(void* user, ClientInventoryView* out) { *out = static_cast<Harness*>(user)->inventory; return 1; }
int read_touch(void* user, EmbeddedTouchInput* out) { *out = static_cast<Harness*>(user)->touch; return 1; }
int compute_ai(void*, const WorldSnapshot*, const InventoryState*, EmbeddedAiInput* out) { out->propose_attack = true; out->propose_place = true; out->place_emergency_allowed = true; return 1; }
int submit_actions(void* user, const EmbeddedActionFrame* in) { static_cast<Harness*>(user)->last = *in; return 1; }
int register_tick(void* user, void (*cb)(void*), void* cb_user) {
    auto* h = static_cast<Harness*>(user); h->registered = true; h->tick_cb = cb; h->tick_user = cb_user; return 1;
}
int image_info(void* user, ClientImageInfo* out) {
    auto* h = static_cast<Harness*>(user);
    h->image.base = 0x1000;
    h->image.mapped_size = 0x2000;
    h->image.library_name = "libminecraftpe.so";
    h->image.build_id_hex = "deadbeef";
    h->image.elf_class = 2U;
    h->image.machine = 183U;
    h->image.executable_segments.clear();
    h->image.executable_segments.push_back(ClientExecutableSegment{h->image.base, h->image.mapped_size, 0x5U});
    *out = h->image;
    return 1;
}

EmbeddedClientHostApi api_for(Harness& h) {
    EmbeddedClientHostApi api;
    api.abi_version = kEmbeddedClientAbiVersion;
    api.protocol = kEmbeddedClientProtocol;
    api.game_version = kEmbeddedClientGameVersion;
    api.library_name = kEmbeddedClientLibrary;
    api.user = &h;
    api.read_client_state = read_state;
    api.read_inventory = read_inventory;
    api.read_touch = read_touch;
    api.compute_ai = compute_ai;
    api.submit_actions = submit_actions;
    api.register_tick_callback = register_tick;
    api.get_image_info = image_info;
    return api;
}

void test_attach_and_tick() {
    Harness h;
    h.state.tick = 10;
    h.state.joined = true;
    h.state.player.health = 20;
    h.state.player.on_ground = true;
    h.touch.attack = true;
    h.touch.move_y = 1.0;
    h.inventory.inventory.has_pearl = true;

    EmbeddedBedrockClientIntegration runtime;
    std::string error;
    CHECK(runtime.attach(api_for(h), &error));
    CHECK(runtime.install_tick_callback(&error));
    CHECK(h.registered);
    h.tick_cb(h.tick_user);
    CHECK(runtime.tick_count() == 1);
    CHECK(h.last.movement);
    CHECK(h.last.attack);
    CHECK(!h.last.place);
}

void test_version_and_callback_validation() {
    Harness h;
    auto api = api_for(h);
    std::string error;
    api.protocol = 2192;
    EmbeddedBedrockClientIntegration runtime;
    CHECK(!runtime.attach(api, &error));
    CHECK(error.find("protocol") != std::string::npos);

    api = api_for(h);
    api.read_touch = nullptr;
    error.clear();
    CHECK(!runtime.attach(api, &error));
    CHECK(error.find("callbacks") != std::string::npos);
}

void test_state_collections() {
    Harness h;
    ClientEntityView entities[2];
    entities[0].runtime_id = 1; entities[0].hostile = true; entities[0].alive = true; entities[0].position.x = 1.0;
    entities[1].runtime_id = 2; entities[1].hostile = false; entities[1].position.z = 2.0;
    ClientBlockView blocks[2];
    blocks[0].x = 1; blocks[0].y = 64; blocks[0].z = 1; blocks[0].solid = true;
    blocks[1].x = 2; blocks[1].y = 64; blocks[1].z = 1; blocks[1].replaceable = true;
    h.state.entities = entities; h.state.entity_count = 2;
    h.state.blocks = blocks; h.state.block_count = 2;
    std::string error;
    EmbeddedBedrockClientIntegration runtime;
    CHECK(runtime.attach(api_for(h), &error));
    CHECK(runtime.tick(&error));
    CHECK(runtime.tick_count() == 1);
}

int failing_submit(void*, const EmbeddedActionFrame*) { return 0; }
void test_emergency_place_and_attack_gate() {
    Harness h;
    h.state.player.health = 2.0;
    h.state.player.airborne = true;
    h.state.player.velocity.y = -9.0;
    h.touch.attack = true;
    std::string error;
    CHECK(api_for(h).compute_ai != nullptr);
    auto api = api_for(h);
    api.compute_ai = [](void*, const WorldSnapshot*, const InventoryState*, EmbeddedAiInput* out) {
        out->propose_attack = true;
        out->propose_place = true;
        out->place_emergency_allowed = true;
        out->emergency = true;
        return 1;
    };
    EmbeddedBedrockClientIntegration runtime;
    CHECK(runtime.attach(api, &error));
    CHECK(runtime.tick(&error));
    CHECK(h.last.attack);
    CHECK(h.last.place);
}

void test_callback_failure() {
    Harness h;
    EmbeddedClientHostApi api = api_for(h);
    api.submit_actions = failing_submit;
    std::string error;
    EmbeddedBedrockClientIntegration runtime;
    CHECK(runtime.attach(api, &error));
    CHECK(!runtime.tick(&error));
    CHECK(error.find("action callback") != std::string::npos);
}

void test_exported_abi() {
    const auto handle = bedrockai_client_integration_create();
    CHECK(handle != 0);
    Harness h;
    auto api = api_for(h);
    BedrockAiEmbeddedClientHost host{};
    host.abi_version = api.abi_version;
    host.protocol = api.protocol;
    host.game_version = api.game_version;
    host.library_name = api.library_name;
    host.user = api.user;
    host.read_client_state = read_state;
    host.read_inventory = read_inventory;
    host.read_touch = read_touch;
    host.compute_ai = compute_ai;
    host.submit_actions = submit_actions;
    host.register_tick_callback = register_tick;
    host.get_image_info = image_info;
    char error[256]{};
    if (bedrockai_client_integration_attach(handle, &host, error, sizeof(error)) != 1) std::abort();
    if (bedrockai_client_integration_tick(handle, error, sizeof(error)) != 1) std::abort();
    if (bedrockai_client_integration_tick_count(handle) != 1) std::abort();
    bedrockai_client_integration_destroy(handle);
}

int main() {
    test_attach_and_tick();
    test_version_and_callback_validation();
    test_state_collections();
    test_emergency_place_and_attack_gate();
    test_callback_failure();
    test_exported_abi();
    return 0;
}
