#include "core/wire/movement_feedback_bridge.hpp"

namespace bedrockai::wire {

bool MovementFeedbackBridge::process_frame(Bedrock2193ObservationDecoder& decoder,
                                           const TransportFrame& frame,
                                           MovementActionSender& sender,
                                           ObservationDecodeReport* report_out,
                                           bool* action_acknowledged,
                                           std::string* error) const {
    if (action_acknowledged != nullptr) *action_acknowledged = false;

    ObservationDecodeReport local_report;
    ObservationDecodeReport& report = report_out != nullptr ? *report_out : local_report;
    if (!decoder.accept_frame(frame, &report, error)) return false;

    if (report.local_player_input_tick.has_value()) {
        const bool acknowledged = sender.observe_server_tick(*report.local_player_input_tick);
        if (action_acknowledged != nullptr) *action_acknowledged = acknowledged;
    }
    return true;
}

} // namespace bedrockai::wire
