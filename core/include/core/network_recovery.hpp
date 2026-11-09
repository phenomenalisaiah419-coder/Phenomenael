#pragma once

#include "transport.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace bedrockai {

// Host-facing lifecycle state used by Android network callbacks. The controller
// never guesses that a socket is still usable after the underlying network changes.
enum class HostNetworkState : std::uint8_t {
    Unknown = 0,
    Available = 1,
    Lost = 2
};

class TransportRecoveryCoordinator {
public:
    using ReconnectHandler = std::function<bool(std::string*)>;
    using StateHandler = std::function<void(HostNetworkState)>;

    explicit TransportRecoveryCoordinator(std::uint32_t max_reconnect_attempts = 3);

    void set_reconnect_handler(ReconnectHandler handler) { reconnect_ = std::move(handler); }
    void set_state_handler(StateHandler handler) { state_handler_ = std::move(handler); }

    void on_network_lost();
    bool on_network_available(std::string* error = nullptr);
    bool on_network_available_with_hint(std::uint64_t network_generation, std::string* error = nullptr);

    HostNetworkState state() const { return state_; }
    std::uint64_t generation() const { return generation_; }
    std::uint32_t reconnect_attempts() const { return reconnect_attempts_; }
    bool recovery_required() const { return recovery_required_; }
    void reset();

private:
    std::uint32_t max_reconnect_attempts_ = 3;
    HostNetworkState state_ = HostNetworkState::Unknown;
    std::uint64_t generation_ = 0;
    std::uint32_t reconnect_attempts_ = 0;
    bool recovery_required_ = false;
    ReconnectHandler reconnect_{};
    StateHandler state_handler_{};

    void publish_state(HostNetworkState state);
};

} // namespace bedrockai
