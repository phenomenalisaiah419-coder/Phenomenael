#include "core/client_abi_adapter.hpp"

#include <cmath>
#include <utility>

namespace bedrockai {
namespace {

void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

template <typename FunctionPointer>
bool validate_callback(FunctionPointer fn, const char* name, std::string* error) {
    if (fn == nullptr) {
        set_error(error, std::string("exact client ABI callback missing: ") + name);
        return false;
    }
    return true;
}

} // namespace

bool ClientAbiAdapter::validate(const ClientAbiTable& table, std::string* error) {
    if (table.version != kClientAbiAdapterVersion) {
        set_error(error, "client ABI adapter version mismatch");
        return false;
    }
    if (table.protocol != kEmbeddedClientProtocol) {
        set_error(error, "client ABI adapter protocol mismatch");
        return false;
    }
    if (table.pointer_width_bytes != kAarch64PointerWidthBytes) {
        set_error(error, "client ABI adapter requires 64-bit pointers");
        return false;
    }
    if (table.client_context == nullptr) {
        set_error(error, "client ABI adapter has no client context");
        return false;
    }
    if (!validate_callback(table.tick, "tick", error)) return false;
    if (!validate_callback(table.read_state, "read_state", error)) return false;
    if (!validate_callback(table.read_inventory, "read_inventory", error)) return false;
    if (!validate_callback(table.read_touch, "read_touch", error)) return false;
    if (!validate_callback(table.submit_actions, "submit_actions", error)) return false;
    return true;
}

bool ClientAbiAdapter::attach(const ClientAbiTable& table, std::string* error) {
    detach();
    if (!validate(table, error)) return false;
    table_ = table;
    attached_ = true;
    return true;
}

void ClientAbiAdapter::detach() {
    table_ = ClientAbiTable{};
    attached_ = false;
}

bool ClientAbiAdapter::invoke_read_state(ClientStateView& out, std::string* error) const {
    if (!attached_) {
        set_error(error, "client ABI adapter is not attached");
        return false;
    }
    out = ClientStateView{};
    if (table_.read_state(table_.client_context, &out) == 0) {
        set_error(error, "client ABI read_state callback failed");
        return false;
    }
    return true;
}

bool ClientAbiAdapter::invoke_read_inventory(ClientInventoryView& out, std::string* error) const {
    if (!attached_) {
        set_error(error, "client ABI adapter is not attached");
        return false;
    }
    out = ClientInventoryView{};
    if (table_.read_inventory(table_.client_context, &out) == 0) {
        set_error(error, "client ABI read_inventory callback failed");
        return false;
    }
    return true;
}

bool ClientAbiAdapter::invoke_read_touch(EmbeddedTouchInput& out, std::string* error) const {
    if (!attached_) {
        set_error(error, "client ABI adapter is not attached");
        return false;
    }
    out = EmbeddedTouchInput{};
    if (table_.read_touch(table_.client_context, &out) == 0) {
        set_error(error, "client ABI read_touch callback failed");
        return false;
    }
    return true;
}

bool ClientAbiAdapter::invoke_submit_actions(const EmbeddedActionFrame& actions, std::string* error) const {
    if (!attached_) {
        set_error(error, "client ABI adapter is not attached");
        return false;
    }
    if (table_.submit_actions(table_.client_context, &actions) == 0) {
        set_error(error, "client ABI submit_actions callback failed");
        return false;
    }
    return true;
}

bool ClientAbiTickRunner::run(
    const ClientAbiAdapter& adapter,
    EmbeddedClientControllerBridge& bridge,
    const ClientAbiTable& table,
    const std::function<bool(const WorldSnapshot&, const InventoryState&, EmbeddedAiInput&, std::string*)>& compute_ai,
    EmbeddedActionFrame& output,
    ClientAbiHookDispatchResult& result) const {
    result = ClientAbiHookDispatchResult{};
    if (!adapter.attached()) {
        result.error = "client ABI adapter is not attached";
        return false;
    }
    if (adapter.client_context() != table.client_context || adapter.original_tick() != table.tick) {
        result.error = "client ABI table changed after adapter attach";
        return false;
    }
    if (!compute_ai) {
        result.error = "client ABI AI callback is missing";
        return false;
    }

    ClientNativeFrame frame;
    if (!adapter.invoke_read_state(frame.state, &result.error)) return false;
    result.state_read = true;
    if (!adapter.invoke_read_inventory(frame.inventory, &result.error)) return false;
    result.inventory_read = true;
    if (!adapter.invoke_read_touch(frame.touch, &result.error)) return false;
    result.touch_read = true;
    frame.tick = frame.state.tick;
    if (!run_captured(frame, bridge, compute_ai, output, result)) return false;
    if (!adapter.invoke_submit_actions(output, &result.error)) return false;
    result.actions_submitted = true;
    return true;
}

bool ClientAbiTickRunner::run_captured(
    const ClientNativeFrame& frame,
    EmbeddedClientControllerBridge& bridge,
    const std::function<bool(const WorldSnapshot&, const InventoryState&, EmbeddedAiInput&, std::string*)>& compute_ai,
    EmbeddedActionFrame& output,
    ClientAbiHookDispatchResult& result) const {
    result.error.clear();
    result.actions_submitted = false;
    if (!compute_ai) {
        result.error = "captured client frame AI callback is missing";
        return false;
    }
    if (frame.tick != frame.state.tick) {
        result.error = "captured native frame tick does not match ReadState tick";
        return false;
    }
    if (frame.state.entity_count > 4096U || frame.state.block_count > 16384U ||
        (frame.state.entity_count != 0U && frame.state.entities == nullptr) ||
        (frame.state.block_count != 0U && frame.state.blocks == nullptr)) {
        result.error = "captured native frame contains invalid collection bounds";
        return false;
    }
    if (!std::isfinite(frame.state.player.health) || frame.state.player.health < 0.0) {
        result.error = "captured native frame supplied invalid player health";
        return false;
    }

    WorldSnapshot snapshot;
    snapshot.tick = frame.tick;
    snapshot.joined = frame.state.joined;
    snapshot.world.dimension = frame.state.dimension;
    snapshot.player = frame.state.player;
    snapshot.last_correction = frame.state.last_correction;

    for (std::size_t i = 0U; i < frame.state.entity_count; ++i) {
        const auto& source = frame.state.entities[i];
        if (!std::isfinite(source.health) || source.health < 0.0 ||
            !std::isfinite(source.position.x) || !std::isfinite(source.position.y) || !std::isfinite(source.position.z) ||
            !std::isfinite(source.velocity.x) || !std::isfinite(source.velocity.y) || !std::isfinite(source.velocity.z)) {
            result.error = "captured native frame contains invalid entity state";
            return false;
        }
        EntityState entity;
        entity.runtime_id = source.runtime_id;
        entity.type_id = source.type_id;
        entity.position = source.position;
        entity.velocity = source.velocity;
        entity.health = source.health;
        entity.hostile = source.hostile;
        entity.alive = source.alive;
        const auto inserted = snapshot.world.entities.emplace(entity.runtime_id, entity);
        if (!inserted.second) {
            result.error = "captured native frame contains duplicate entity runtime IDs";
            return false;
        }
    }
    for (std::size_t i = 0U; i < frame.state.block_count; ++i) {
        const auto& source = frame.state.blocks[i];
        BlockObservation block;
        block.x = source.x;
        block.y = source.y;
        block.z = source.z;
        block.runtime_id = source.runtime_id;
        block.solid = source.solid;
        block.replaceable = source.replaceable;
        block.observed_tick = frame.tick;
        const auto key = block_key(block.x, block.y, block.z);
        const auto inserted = snapshot.world.blocks.emplace(key, block);
        if (!inserted.second &&
            (inserted.first->second.x != block.x || inserted.first->second.y != block.y || inserted.first->second.z != block.z)) {
            result.error = "captured native frame contains colliding block keys";
            return false;
        }
        if (!inserted.second) {
            result.error = "captured native frame contains duplicate block observations";
            return false;
        }
    }

    EmbeddedAiInput ai;
    if (!compute_ai(snapshot, frame.inventory.inventory, ai, &result.error)) return false;
    if (!bridge.tick(snapshot, frame.inventory.inventory, frame.touch, ai, output, &result.error)) return false;
    if (!std::isfinite(output.forward) || !std::isfinite(output.strafe) ||
        !std::isfinite(output.yaw_delta) || !std::isfinite(output.pitch_delta)) {
        result.error = "controller produced non-finite action values";
        return false;
    }
    return true;
}

} // namespace bedrockai
