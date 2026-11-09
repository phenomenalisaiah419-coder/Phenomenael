#pragma once

#include "client_abi_adapter.hpp"
#include "hook_binding.hpp"
#include "client_hook_event.hpp"
#include "client_native_frame.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <functional>

namespace bedrockai {

struct ClientHookDispatchStats {
    std::uint64_t ticks = 0;
    std::uint64_t state_reads = 0;
    std::uint64_t inventory_reads = 0;
    std::uint64_t touch_reads = 0;
    std::uint64_t action_submissions = 0;
    std::uint64_t rejected_calls = 0;
};

// Runtime dispatcher installed behind the exact-ABI client hook installer.
// The core never guesses Mojang signatures: the modified client converts its
// exact hook ABI into these typed events, then this dispatcher executes the
// controller path.
class ClientHookDispatcher {
public:
    bool attach(const ClientAbiTable& table, std::string* error = nullptr);
    bool attach(const ClientAbiTable& table, EmbeddedClientControllerBridge& bridge,
                std::function<bool(const WorldSnapshot&, const InventoryState&, EmbeddedAiInput&, std::string*)> compute_ai,
                std::string* error = nullptr);
    void detach();
    bool attached() const { return attached_.load(std::memory_order_acquire); }

    bool on_tick(ClientAbiTickRunner& runner,
                 EmbeddedClientControllerBridge& bridge,
                 const std::function<bool(const WorldSnapshot&, const InventoryState&, EmbeddedAiInput&, std::string*)>& compute_ai,
                 EmbeddedActionFrame& output,
                 std::string* error = nullptr);

    bool on_read_state(ClientStateView& out, std::string* error = nullptr);
    bool on_read_inventory(ClientInventoryView& out, std::string* error = nullptr);
    bool on_read_touch(EmbeddedTouchInput& out, std::string* error = nullptr);
    bool on_submit_actions(const EmbeddedActionFrame& actions, std::string* error = nullptr);

    int dispatch_tick(const ClientHookEventMetadata* metadata, std::string* error = nullptr);
    int dispatch_read_state(const ClientHookEventMetadata* metadata, const ClientStateView* state, std::string* error = nullptr);
    int dispatch_read_inventory(const ClientHookEventMetadata* metadata, const ClientInventoryView* inventory, std::string* error = nullptr);
    int dispatch_read_touch(const ClientHookEventMetadata* metadata, const EmbeddedTouchInput* touch, std::string* error = nullptr);
    int dispatch_submit_actions(const ClientHookEventMetadata* metadata, const EmbeddedActionFrame* actions, std::string* error = nullptr);

    const EmbeddedActionFrame& last_actions() const { return last_actions_; }
    ClientHookDispatchStats stats() const;
    void require_coherent_native_frames() { coherent_native_frames_required_.store(true, std::memory_order_release); }

private:
    ClientAbiAdapter adapter_{};
    EmbeddedClientControllerBridge* bridge_ = nullptr;
    std::function<bool(const WorldSnapshot&, const InventoryState&, EmbeddedAiInput&, std::string*)> compute_ai_{};
    ClientAbiTickRunner runner_{};
    EmbeddedActionFrame last_actions_{};
    ClientAbiTable table_{};
    ClientNativeFrameAssembler frame_{};
    std::atomic<bool> attached_{false};
    std::atomic<std::uint64_t> ticks_{0};
    std::atomic<std::uint64_t> state_reads_{0};
    std::atomic<std::uint64_t> inventory_reads_{0};
    std::atomic<std::uint64_t> touch_reads_{0};
    std::atomic<std::uint64_t> action_submissions_{0};
    std::atomic<std::uint64_t> rejected_calls_{0};
    std::atomic<bool> in_tick_{false};
    std::atomic<bool> coherent_native_frames_required_{false};
};

} // namespace bedrockai
