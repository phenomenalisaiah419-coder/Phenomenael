#include "core/client_hook_dispatch.hpp"

#include <cmath>
#include <utility>

namespace bedrockai {
namespace {
void set_error(std::string* error, std::string value) {
    if (error) *error = std::move(value);
}
}

bool ClientHookDispatcher::attach(const ClientAbiTable& table, std::string* error) {
    detach();
    if (!adapter_.attach(table, error)) return false;
    table_ = table;
    frame_.reset();
    ticks_.store(0U, std::memory_order_release);
    state_reads_.store(0U, std::memory_order_release);
    inventory_reads_.store(0U, std::memory_order_release);
    touch_reads_.store(0U, std::memory_order_release);
    action_submissions_.store(0U, std::memory_order_release);
    rejected_calls_.store(0U, std::memory_order_release);
    attached_.store(true, std::memory_order_release);
    return true;
}

bool ClientHookDispatcher::attach(const ClientAbiTable& table, EmbeddedClientControllerBridge& bridge,
                                   std::function<bool(const WorldSnapshot&, const InventoryState&, EmbeddedAiInput&, std::string*)> compute_ai,
                                   std::string* error) {
    detach();
    if (!compute_ai) { set_error(error, "client hook dispatcher AI callback is missing"); return false; }
    if (!adapter_.attach(table, error)) return false;
    bridge_ = &bridge;
    compute_ai_ = std::move(compute_ai);
    table_ = table;
    frame_.reset();
    ticks_.store(0U, std::memory_order_release);
    state_reads_.store(0U, std::memory_order_release);
    inventory_reads_.store(0U, std::memory_order_release);
    touch_reads_.store(0U, std::memory_order_release);
    action_submissions_.store(0U, std::memory_order_release);
    rejected_calls_.store(0U, std::memory_order_release);
    attached_.store(true, std::memory_order_release);
    return true;
}

void ClientHookDispatcher::detach() {
    attached_.store(false, std::memory_order_release);
    adapter_.detach();
    table_ = ClientAbiTable{};
    bridge_ = nullptr;
    compute_ai_ = {};
    frame_.reset();
    last_actions_ = EmbeddedActionFrame{};
    in_tick_.store(false, std::memory_order_release);
    coherent_native_frames_required_.store(false, std::memory_order_release);
}

bool ClientHookDispatcher::on_tick(
    ClientAbiTickRunner& runner,
    EmbeddedClientControllerBridge& bridge,
    const std::function<bool(const WorldSnapshot&, const InventoryState&, EmbeddedAiInput&, std::string*)>& compute_ai,
    EmbeddedActionFrame& output,
    std::string* error) {
    if (!attached_.load(std::memory_order_acquire)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "client hook dispatcher is not attached");
        return false;
    }
    ClientAbiHookDispatchResult result;
    if (!runner.run(adapter_, bridge, table_, compute_ai, output, result)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, result.error);
        return false;
    }
    ticks_.fetch_add(1U, std::memory_order_relaxed);
    state_reads_.fetch_add(1U, std::memory_order_relaxed);
    inventory_reads_.fetch_add(1U, std::memory_order_relaxed);
    touch_reads_.fetch_add(1U, std::memory_order_relaxed);
    action_submissions_.fetch_add(1U, std::memory_order_relaxed);
    return true;
}

bool ClientHookDispatcher::on_read_state(ClientStateView& out, std::string* error) {
    if (!attached_.load(std::memory_order_acquire)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "client hook dispatcher is not attached");
        return false;
    }
    if (!adapter_.invoke_read_state(out, error)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    state_reads_.fetch_add(1U, std::memory_order_relaxed);
    return true;
}

bool ClientHookDispatcher::on_read_inventory(ClientInventoryView& out, std::string* error) {
    if (!attached_.load(std::memory_order_acquire)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "client hook dispatcher is not attached");
        return false;
    }
    if (!adapter_.invoke_read_inventory(out, error)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    inventory_reads_.fetch_add(1U, std::memory_order_relaxed);
    return true;
}

bool ClientHookDispatcher::on_read_touch(EmbeddedTouchInput& out, std::string* error) {
    if (!attached_.load(std::memory_order_acquire)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "client hook dispatcher is not attached");
        return false;
    }
    if (!adapter_.invoke_read_touch(out, error)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    touch_reads_.fetch_add(1U, std::memory_order_relaxed);
    return true;
}

bool ClientHookDispatcher::on_submit_actions(const EmbeddedActionFrame& actions, std::string* error) {
    if (!attached_.load(std::memory_order_acquire)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "client hook dispatcher is not attached");
        return false;
    }
    if (!adapter_.invoke_submit_actions(actions, error)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    action_submissions_.fetch_add(1U, std::memory_order_relaxed);
    return true;
}

int ClientHookDispatcher::dispatch_tick(const ClientHookEventMetadata* metadata, std::string* error) {
    if (!attached_.load(std::memory_order_acquire) || bridge_ == nullptr || !compute_ai_) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "client hook dispatcher is not ready for Tick");
        return 0;
    }
    bool expected = false;
    if (!in_tick_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "reentrant client Tick dispatch rejected");
        return 0;
    }
    if (!metadata) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "invalid Tick hook event metadata");
        return 0;
    }
    ClientNativeFrame frame;
    std::string local_error;
    bool ok = false;
    bool used_captured_frame = false;
    ClientAbiHookDispatchResult result;
    if (!coherent_native_frames_required_.load(std::memory_order_acquire) &&
        !frame_.state_ready() && !frame_.inventory_ready() && !frame_.touch_ready()) {
        ok = on_tick(runner_, *bridge_, compute_ai_, last_actions_, &local_error);
    } else {
        used_captured_frame = true;
        if (!frame_.consume(*metadata, frame, &local_error)) {
            in_tick_.store(false, std::memory_order_release);
            rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
            set_error(error, local_error);
            return 0;
        }
        ok = runner_.run_captured(frame, *bridge_, compute_ai_, last_actions_, result);
        if (ok) ok = on_submit_actions(last_actions_, &local_error);
        if (!ok && local_error.empty()) local_error = result.error;
    }
    if (ok && used_captured_frame) ticks_.fetch_add(1U, std::memory_order_relaxed);
    in_tick_.store(false, std::memory_order_release);
    if (!ok) set_error(error, local_error);
    return ok ? 1 : 0;
}

int ClientHookDispatcher::dispatch_read_state(const ClientHookEventMetadata* metadata, const ClientStateView* state, std::string* error) {
    if (!attached_.load(std::memory_order_acquire) || !metadata || !state) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "invalid ReadState hook event");
        return 0;
    }
    if (!frame_.capture_state(*metadata, *state, error)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        return 0;
    }
    state_reads_.fetch_add(1U, std::memory_order_relaxed);
    return 1;
}

int ClientHookDispatcher::dispatch_read_inventory(const ClientHookEventMetadata* metadata, const ClientInventoryView* inventory, std::string* error) {
    if (!attached_.load(std::memory_order_acquire) || !metadata || !inventory) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "invalid ReadInventory hook event");
        return 0;
    }
    if (!frame_.capture_inventory(*metadata, *inventory, error)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        return 0;
    }
    inventory_reads_.fetch_add(1U, std::memory_order_relaxed);
    return 1;
}

int ClientHookDispatcher::dispatch_read_touch(const ClientHookEventMetadata* metadata, const EmbeddedTouchInput* touch, std::string* error) {
    if (!attached_.load(std::memory_order_acquire) || !metadata || !touch) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "invalid ReadTouch hook event");
        return 0;
    }
    if (!frame_.capture_touch(*metadata, *touch, error)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        return 0;
    }
    touch_reads_.fetch_add(1U, std::memory_order_relaxed);
    return 1;
}

int ClientHookDispatcher::dispatch_submit_actions(const ClientHookEventMetadata* metadata, const EmbeddedActionFrame* actions, std::string* error) {
    if (!attached_.load(std::memory_order_acquire) || !metadata || !actions) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "invalid SubmitActions hook event");
        return 0;
    }
    if (metadata->version != kClientHookEventAbiVersion || metadata->protocol != kEmbeddedClientProtocol) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "SubmitActions event metadata mismatch");
        return 0;
    }
    if (!std::isfinite(actions->forward) || !std::isfinite(actions->strafe) ||
        !std::isfinite(actions->yaw_delta) || !std::isfinite(actions->pitch_delta)) {
        rejected_calls_.fetch_add(1U, std::memory_order_relaxed);
        set_error(error, "invalid SubmitActions values");
        return 0;
    }
    action_submissions_.fetch_add(1U, std::memory_order_relaxed);
    return 1;
}

ClientHookDispatchStats ClientHookDispatcher::stats() const {
    ClientHookDispatchStats value;
    value.ticks = ticks_.load(std::memory_order_acquire);
    value.state_reads = state_reads_.load(std::memory_order_acquire);
    value.inventory_reads = inventory_reads_.load(std::memory_order_acquire);
    value.touch_reads = touch_reads_.load(std::memory_order_acquire);
    value.action_submissions = action_submissions_.load(std::memory_order_acquire);
    value.rejected_calls = rejected_calls_.load(std::memory_order_acquire);
    return value;
}

} // namespace bedrockai
