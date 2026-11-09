#include "core/network_recovery.hpp"

namespace bedrockai {

TransportRecoveryCoordinator::TransportRecoveryCoordinator(std::uint32_t max_reconnect_attempts)
    : max_reconnect_attempts_(max_reconnect_attempts) {}

void TransportRecoveryCoordinator::publish_state(HostNetworkState state) {
    state_ = state;
    if (state_handler_) state_handler_(state_);
}

void TransportRecoveryCoordinator::on_network_lost() {
    ++generation_;
    reconnect_attempts_ = 0;
    recovery_required_ = true;
    publish_state(HostNetworkState::Lost);
}

bool TransportRecoveryCoordinator::on_network_available(std::string* error) {
    return on_network_available_with_hint(generation_, error);
}

bool TransportRecoveryCoordinator::on_network_available_with_hint(std::uint64_t network_generation,
                                                                   std::string* error) {
    if (network_generation < generation_) {
        if (error) *error = "stale Android network-generation callback";
        return false;
    }
    if (network_generation > generation_) generation_ = network_generation;
    publish_state(HostNetworkState::Available);
    if (!recovery_required_) return true;
    if (!reconnect_) {
        if (error) *error = "network recovery requested but no reconnect handler is configured";
        return false;
    }
    if (reconnect_attempts_ >= max_reconnect_attempts_) {
        if (error) *error = "network recovery retry limit reached";
        return false;
    }
    ++reconnect_attempts_;
    if (!reconnect_(error)) return false;
    recovery_required_ = false;
    reconnect_attempts_ = 0;
    return true;
}

void TransportRecoveryCoordinator::reset() {
    state_ = HostNetworkState::Unknown;
    generation_ = 0;
    reconnect_attempts_ = 0;
    recovery_required_ = false;
}

} // namespace bedrockai
