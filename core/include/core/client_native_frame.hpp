#pragma once

#include "bedrock_client_integration.hpp"
#include "client_hook_event.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace bedrockai {

// Coherent native-hook frame assembled from the real five-site translated ABI.
// State establishes the tick generation; inventory and touch are accepted only
// into that pending generation. Consumption is single-use and fail-closed.
struct ClientNativeFrame {
    std::uint64_t tick = 0U;
    ClientStateView state{};
    ClientInventoryView inventory{};
    EmbeddedTouchInput touch{};
};

class ClientNativeFrameAssembler {
public:
    ClientNativeFrameAssembler() = default;
    ClientNativeFrameAssembler(const ClientNativeFrameAssembler&) = delete;
    ClientNativeFrameAssembler& operator=(const ClientNativeFrameAssembler&) = delete;

    bool capture_state(const ClientHookEventMetadata& metadata, const ClientStateView& state, std::string* error = nullptr);
    bool capture_inventory(const ClientHookEventMetadata& metadata, const ClientInventoryView& inventory, std::string* error = nullptr);
    bool capture_touch(const ClientHookEventMetadata& metadata, const EmbeddedTouchInput& touch, std::string* error = nullptr);
    bool consume(const ClientHookEventMetadata& metadata, ClientNativeFrame& out, std::string* error = nullptr);

    std::uint64_t last_seen_tick() const { return last_seen_tick_; }
    std::uint64_t last_consumed_tick() const { return last_consumed_tick_; }
    bool state_ready() const { return state_ready_.load(std::memory_order_acquire); }
    bool inventory_ready() const { return inventory_ready_.load(std::memory_order_acquire); }
    bool touch_ready() const { return touch_ready_.load(std::memory_order_acquire); }

    void reset();

private:
    class Guard {
    public:
        explicit Guard(std::atomic_flag& flag) : flag_(flag) {
            locked_ = flag_.test_and_set(std::memory_order_acquire) == false;
        }
        ~Guard() { if (locked_) flag_.clear(std::memory_order_release); }
        bool locked() const { return locked_; }
    private:
        std::atomic_flag& flag_;
        bool locked_ = false;
    };

    static bool finite(double value);
    static bool validate_state(const ClientStateView& state, std::string* error);
    static bool validate_inventory(const ClientInventoryView& inventory, std::string* error);
    static bool validate_touch(const EmbeddedTouchInput& touch, std::string* error);
    static void set_error(std::string* error, const char* message);

    bool validate_metadata(const ClientHookEventMetadata& metadata, std::string* error);
    bool begin_state_locked(const ClientHookEventMetadata& metadata, const ClientStateView& state, std::string* error);

    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::uint64_t last_seen_tick_ = 0U;
    std::uint64_t last_consumed_tick_ = 0U;
    ClientStateView state_{};
    ClientInventoryView inventory_{};
    EmbeddedTouchInput touch_{};
    bool has_seen_tick_ = false;
    std::atomic<bool> state_ready_{false};
    std::atomic<bool> inventory_ready_{false};
    std::atomic<bool> touch_ready_{false};
};

} // namespace bedrockai
