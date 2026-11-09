#pragma once

#include "player_auth_input.hpp"
#include "core/network_engine.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace bedrockai::wire {

// A deliberately narrow action boundary: one movement/rotation PlayerAuthInput
// at a time. A send remains pending until authoritative server tick feedback
// acknowledges it; an uncertain send failure is not automatically retried.
class MovementActionSender {
public:
    bool send_once(Protocol2193NetworkEngine& engine,
                   const PlayerAuthInputPacket& packet,
                   std::string* error = nullptr);
    bool observe_server_tick(std::uint64_t server_tick);
    void reset();

    bool awaiting_feedback() const { return pending_tick_.has_value(); }
    std::optional<std::uint64_t> pending_tick() const { return pending_tick_; }
    std::uint64_t last_submitted_tick() const { return last_submitted_tick_; }

private:
    std::optional<std::uint64_t> pending_tick_;
    std::uint64_t last_submitted_tick_ = 0;
    bool has_submitted_ = false;
};

} // namespace bedrockai::wire
