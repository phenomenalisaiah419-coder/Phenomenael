#include "core/client_abi_adapter.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace bedrockai;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "CHECK failed: %s at %d\n", #c, __LINE__); std::abort(); } } while (0)

struct FakeClient {
    std::uint64_t reads = 0;
    std::uint64_t submits = 0;
    bool attack_touch = true;
    EmbeddedActionFrame last{};
};

int read_state(void* raw, ClientStateView* out) {
    auto* client = static_cast<FakeClient*>(raw);
    if (!client || !out) return 0;
    *out = ClientStateView{};
    out->tick = ++client->reads;
    out->joined = true;
    out->player.health = 20.0;
    return 1;
}

int read_inventory(void* raw, ClientInventoryView* out) {
    auto* client = static_cast<FakeClient*>(raw);
    if (!client || !out) return 0;
    *out = ClientInventoryView{};
    return 1;
}

int read_touch(void* raw, EmbeddedTouchInput* out) {
    auto* client = static_cast<FakeClient*>(raw);
    if (!client || !out) return 0;
    *out = EmbeddedTouchInput{};
    out->attack = client->attack_touch;
    out->move_y = 1.0;
    return 1;
}

void tick_fn(void*) {}

int submit_actions(void* raw, const EmbeddedActionFrame* actions) {
    auto* client = static_cast<FakeClient*>(raw);
    if (!client || !actions) return 0;
    client->last = *actions;
    ++client->submits;
    return 1;
}

void test_adapter_validation() {
    ClientAbiTable table;
    std::string error;
    CHECK(!ClientAbiAdapter::validate(table, &error));
    CHECK(error.find("client ABI adapter") != std::string::npos);
}


void test_adapter_rejects_bad_protocol_and_width() {
    FakeClient fake;
    ClientAbiTable table;
    table.client_context = &fake;
    table.tick = tick_fn;
    table.read_state = read_state;
    table.read_inventory = read_inventory;
    table.read_touch = read_touch;
    table.submit_actions = submit_actions;
    std::string error;
    table.protocol = kEmbeddedClientProtocol + 1U;
    CHECK(!ClientAbiAdapter::validate(table, &error));
    CHECK(error.find("protocol mismatch") != std::string::npos);
    table.protocol = kEmbeddedClientProtocol;
    table.pointer_width_bytes = 4U;
    CHECK(!ClientAbiAdapter::validate(table, &error));
    CHECK(error.find("64-bit pointers") != std::string::npos);
}

void test_callback_failure_propagates() {
    FakeClient fake;
    ClientAbiTable table;
    table.client_context = &fake;
    table.tick = tick_fn;
    table.read_state = [](void*, ClientStateView*) { return 0; };
    table.read_inventory = read_inventory;
    table.read_touch = read_touch;
    table.submit_actions = submit_actions;
    ClientAbiAdapter adapter;
    std::string error;
    CHECK(adapter.attach(table, &error));
    ClientStateView state;
    CHECK(!adapter.invoke_read_state(state, &error));
    CHECK(error.find("read_state callback failed") != std::string::npos);
}

void test_real_typed_flow() {
    FakeClient fake;
    ClientAbiTable table;
    table.client_context = &fake;
    table.tick = tick_fn;
    table.read_state = read_state;
    table.read_inventory = read_inventory;
    table.read_touch = read_touch;
    table.submit_actions = submit_actions;

    ClientAbiAdapter adapter;
    std::string error;
    CHECK(adapter.attach(table, &error));
    CHECK(adapter.original_tick() == tick_fn);

    EmbeddedClientControllerBridge bridge;
    ClientAbiTickRunner runner;
    EmbeddedActionFrame output;
    ClientAbiHookDispatchResult result;
    const auto compute_ai = [](const WorldSnapshot&, const InventoryState&, EmbeddedAiInput& ai, std::string*) {
        ai = EmbeddedAiInput{};
        ai.propose_attack = true;
        return true;
    };
    CHECK(runner.run(adapter, bridge, table, compute_ai, output, result));
    CHECK(result.state_read);
    CHECK(result.inventory_read);
    CHECK(result.touch_read);
    CHECK(result.actions_submitted);
    CHECK(fake.submits == 1U);
    CHECK(fake.last.attack);

    fake.attack_touch = false;
    CHECK(runner.run(adapter, bridge, table, compute_ai, output, result));
    CHECK(fake.submits == 2U);
    CHECK(!fake.last.attack);
    CHECK(bridge.tick_count() == 2U);
}

void test_table_mutation_is_rejected() {
    FakeClient first;
    FakeClient second;
    ClientAbiTable table;
    table.client_context = &first;
    table.tick = tick_fn;
    table.read_state = read_state;
    table.read_inventory = read_inventory;
    table.read_touch = read_touch;
    table.submit_actions = submit_actions;

    ClientAbiAdapter adapter;
    std::string error;
    CHECK(adapter.attach(table, &error));
    table.client_context = &second;
    ClientAbiTickRunner runner;
    EmbeddedClientControllerBridge bridge;
    ClientAbiHookDispatchResult result;
    EmbeddedActionFrame output;
    CHECK(!runner.run(adapter, bridge, table,
                      [](const WorldSnapshot&, const InventoryState&, EmbeddedAiInput&, std::string*) { return true; },
                      output, result));
    CHECK(result.error.find("changed after adapter attach") != std::string::npos);
}

int main() {
    test_adapter_validation();
    test_adapter_rejects_bad_protocol_and_width();
    test_callback_failure_propagates();
    test_real_typed_flow();
    test_table_mutation_is_rejected();
    return 0;
}
