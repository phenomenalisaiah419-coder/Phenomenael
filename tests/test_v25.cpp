#include "core/client_hook_dispatch.hpp"
#include "core/client_native_frame.hpp"
#include "core/client_abi_adapter.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

using namespace bedrockai;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"CHECK failed: %s at %d\n",#c,__LINE__); std::abort(); } } while(0)

static ClientHookEventMetadata meta(std::uint64_t tick) { ClientHookEventMetadata value; value.tick = tick; return value; }

static ClientStateView state(std::uint64_t tick) {
    ClientStateView value;
    value.tick = tick;
    value.joined = true;
    value.player.health = 20.0;
    value.player.position = Vec3{1.0, 2.0, 3.0};
    value.player.velocity = Vec3{0.0, 0.0, 0.0};
    value.player.rotation = Rot2{0.0, 90.0};
    value.player.head_yaw = 90.0;
    return value;
}

static int submit(void*, const EmbeddedActionFrame*) { return 1; }

struct DispatchFake {
    std::uint64_t submits = 0U;
    EmbeddedActionFrame last{};
};

static int dispatch_read_state(void*, ClientStateView* out) {
    if (!out) return 0;
    *out = state(1U);
    return 1;
}
static int dispatch_read_inventory(void*, ClientInventoryView* out) { if (!out) return 0; *out = ClientInventoryView{}; return 1; }
static int dispatch_read_touch(void*, EmbeddedTouchInput* out) { if (!out) return 0; *out = EmbeddedTouchInput{}; return 1; }
static void dispatch_tick_identity(void*) {}
static int dispatch_submit(void* user, const EmbeddedActionFrame* action) {
    auto* fake = static_cast<DispatchFake*>(user);
    if (!fake || !action) return 0;
    fake->last = *action;
    ++fake->submits;
    return 1;
}

static void test_coherent_dispatch_path() {
    DispatchFake fake;
    ClientAbiTable table;
    table.client_context = &fake;
    table.tick = dispatch_tick_identity;
    table.read_state = dispatch_read_state;
    table.read_inventory = dispatch_read_inventory;
    table.read_touch = dispatch_read_touch;
    table.submit_actions = dispatch_submit;

    ClientHookDispatcher dispatcher;
    EmbeddedClientControllerBridge bridge;
    std::string error;
    const auto compute_ai = [](const WorldSnapshot&, const InventoryState&, EmbeddedAiInput& out, std::string*) {
        out = EmbeddedAiInput{};
        out.propose_attack = true;
        return true;
    };
    CHECK(dispatcher.attach(table, bridge, compute_ai, &error));
    dispatcher.require_coherent_native_frames();

    auto native_state = state(7U);
    ClientInventoryView inventory;
    EmbeddedTouchInput touch;
    touch.attack = true;
    auto event = meta(7U);
    CHECK(dispatcher.dispatch_read_state(&event, &native_state, &error) == 1);
    CHECK(dispatcher.dispatch_read_inventory(&event, &inventory, &error) == 1);
    CHECK(dispatcher.dispatch_read_touch(&event, &touch, &error) == 1);
    CHECK(dispatcher.dispatch_tick(&event, &error) == 1);
    CHECK(fake.submits == 1U);
    CHECK(fake.last.attack);

    CHECK(dispatcher.dispatch_tick(&event, &error) == 0);
    CHECK(error.find("incomplete") != std::string::npos);
}

static void test_concurrent_capture_is_race_safe() {
    ClientNativeFrameAssembler assembler;
    auto s50 = state(50U);
    std::atomic<int> successes{0};
    std::atomic<int> failures{0};
    std::string e1;
    std::string e2;
    std::thread a([&] { if (assembler.capture_state(meta(50U), s50, &e1)) ++successes; else ++failures; });
    std::thread b([&] { if (assembler.capture_state(meta(50U), s50, &e2)) ++successes; else ++failures; });
    a.join();
    b.join();
    CHECK(successes.load() == 1);
    CHECK(failures.load() == 1);
}

int main() {
    ClientNativeFrameAssembler assembler;
    std::string error;
    ClientInventoryView inventory;
    EmbeddedTouchInput touch;
    touch.attack = true;

    CHECK(!assembler.capture_inventory(meta(42U), inventory, &error));
    CHECK(error.find("before ReadState") != std::string::npos);

    auto s42 = state(42U);
    CHECK(assembler.capture_state(meta(42U), s42, &error));
    CHECK(assembler.capture_inventory(meta(42U), inventory, &error));
    CHECK(assembler.capture_touch(meta(42U), touch, &error));

    ClientNativeFrame frame;
    CHECK(assembler.consume(meta(42U), frame, &error));
    CHECK(frame.tick == 42U);
    CHECK(frame.state.tick == 42U);
    CHECK(frame.touch.attack);
    CHECK(!assembler.state_ready());
    CHECK(!assembler.inventory_ready());
    CHECK(!assembler.touch_ready());
    CHECK(!assembler.consume(meta(42U), frame, &error));
    CHECK(error.find("incomplete") != std::string::npos);

    assembler.reset();
    auto s60 = state(60U);
    CHECK(!assembler.capture_state(meta(61U), s60, &error));
    CHECK(error.find("metadata tick") != std::string::npos);
    CHECK(assembler.capture_state(meta(60U), s60, &error));
    CHECK(!assembler.capture_inventory(meta(61U), inventory, &error));
    CHECK(error.find("does not match pending") != std::string::npos);
    CHECK(assembler.capture_inventory(meta(60U), inventory, &error));
    CHECK(assembler.capture_touch(meta(60U), touch, &error));
    CHECK(!assembler.consume(meta(61U), frame, &error));
    CHECK(error.find("metadata") != std::string::npos);
    CHECK(assembler.consume(meta(60U), frame, &error));

    auto stale60 = state(60U);
    CHECK(!assembler.capture_state(meta(60U), stale60, &error));
    CHECK(error.find("stale") != std::string::npos);

    assembler.reset();
    auto s40 = state(40U);
    CHECK(assembler.capture_state(meta(40U), s40, &error));
    CHECK(!assembler.capture_state(meta(40U), s40, &error));
    CHECK(error.find("duplicate") != std::string::npos);

    auto s41 = state(41U);
    CHECK(assembler.capture_state(meta(41U), s41, &error));
    auto s39 = state(39U);
    CHECK(!assembler.capture_state(meta(39U), s39, &error));
    CHECK(error.find("backwards") != std::string::npos);

    assembler.reset();
    auto s43 = state(43U);
    CHECK(assembler.capture_state(meta(43U), s43, &error));
    CHECK(assembler.capture_inventory(meta(43U), inventory, &error));
    EmbeddedTouchInput bad_touch = touch;
    bad_touch.look_dx = std::numeric_limits<double>::quiet_NaN();
    CHECK(!assembler.capture_touch(meta(43U), bad_touch, &error));
    CHECK(error.find("non-finite") != std::string::npos);
    CHECK(assembler.capture_touch(meta(43U), touch, &error));
    CHECK(assembler.consume(meta(43U), frame, &error));
    CHECK(frame.tick == 43U);

    std::array<ClientEntityView, 2> entities{};
    entities[0].runtime_id = 7U;
    entities[0].health = 20.0;
    entities[1] = entities[0];
    ClientStateView duplicate_entities = state(44U);
    duplicate_entities.entities = entities.data();
    duplicate_entities.entity_count = entities.size();

    ClientNativeFrame bad_frame;
    bad_frame.tick = duplicate_entities.tick;
    bad_frame.state = duplicate_entities;
    bad_frame.inventory = inventory;
    bad_frame.touch = touch;

    EmbeddedClientControllerBridge bridge;
    ClientAbiTickRunner runner;
    ClientAbiHookDispatchResult result;
    const auto no_submit_ai = [](const WorldSnapshot&, const InventoryState&, EmbeddedAiInput& out, std::string*) {
        out = EmbeddedAiInput{};
        return true;
    };
    EmbeddedActionFrame bad_output;
    CHECK(!runner.run_captured(bad_frame, bridge, no_submit_ai, bad_output, result));
    CHECK(result.error.find("duplicate entity") != std::string::npos);

    ClientStateView invalid_state = state(45U);
    invalid_state.player.position.x = std::numeric_limits<double>::infinity();
    CHECK(!assembler.capture_state(meta(45U), invalid_state, &error));
    CHECK(error.find("non-finite player state") != std::string::npos);

    assembler.reset();
    CHECK(assembler.last_seen_tick() == 0U);
    CHECK(assembler.last_consumed_tick() == 0U);

    test_coherent_dispatch_path();
    test_concurrent_capture_is_race_safe();
    (void)submit;
    return 0;
}
