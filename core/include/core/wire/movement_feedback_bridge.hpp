#pragma once

#include "core/bedrock2193_observation_decoder.hpp"
#include "core/wire/movement_action_sender.hpp"

#include <string>

namespace bedrockai::wire {

// Routes validated clientbound frames through the observation decoder and passes
// only explicitly local-player PlayerInputTick evidence to the pending movement
// action gate. A packet's general world/server tick is not sufficient evidence.
class MovementFeedbackBridge {
public:
    bool process_frame(Bedrock2193ObservationDecoder& decoder,
                       const TransportFrame& frame,
                       MovementActionSender& sender,
                       ObservationDecodeReport* report = nullptr,
                       bool* action_acknowledged = nullptr,
                       std::string* error = nullptr) const;
};

} // namespace bedrockai::wire
