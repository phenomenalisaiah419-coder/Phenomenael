#include "core/replay_backend.hpp"

#include <utility>

namespace bedrockai {

ReplayTransportBackend::ReplayTransportBackend(ReplayBackendConfig config)
    : config_(std::move(config)) {}

bool ReplayTransportBackend::load(const std::vector<RecordedFrame>& frames, std::string* error) {
    if (frames.size() > config_.max_frames) {
        if (error) *error = "replay contains too many frames";
        return false;
    }
    frames_ = frames;
    for (const auto& recorded : frames_) {
        if (!recorded.frame.identity.is_target()) {
            if (error) *error = "replay contains a non-target Bedrock identity";
            frames_.clear();
            return false;
        }
        if (recorded.frame.identity.transport != config_.identity.transport &&
            config_.identity.transport != BedrockTransport::Unknown) {
            if (error) *error = "replay transport does not match backend transport";
            frames_.clear();
            return false;
        }
    }
    next_frame_ = 0;
    serverbound_observed_.clear();
    serverbound_expected_cursor_ = 0;
    return true;
}

bool ReplayTransportBackend::load_file(const std::string& path, std::string* error) {
    std::vector<RecordedFrame> frames;
    if (!TransportRecorder::read_file(path, frames, config_.max_frames, error)) return false;
    return load(frames, error);
}

bool ReplayTransportBackend::connect(const ProtocolIdentity& identity, std::string* error) {
    if (!identity.is_target()) {
        if (error) *error = "replay backend requires Bedrock 1.26.50 / 2193";
        state_ = TransportState::Failed;
        return false;
    }
    if (config_.identity.transport != BedrockTransport::Unknown && identity.transport != config_.identity.transport) {
        if (error) *error = "replay backend transport mismatch";
        state_ = TransportState::Failed;
        return false;
    }
    if (frames_.empty()) {
        if (error) *error = "replay backend has no frames";
        state_ = TransportState::Failed;
        return false;
    }
    config_.identity = identity;
    next_frame_ = 0;
    serverbound_observed_.clear();
    serverbound_expected_cursor_ = 0;
    state_ = TransportState::Authenticating;
    if (config_.auto_play) return step(error);
    return true;
}

bool ReplayTransportBackend::send(const TransportFrame& frame, std::string* error) {
    if (state_ == TransportState::Disconnected || state_ == TransportState::Closing || state_ == TransportState::Failed) {
        if (error) *error = "replay backend is not connected";
        return false;
    }
    if (!frame.identity.is_target()) {
        if (error) *error = "replay backend received a non-target serverbound frame";
        return false;
    }
    if (serverbound_observed_.size() >= max_serverbound_) {
        if (error) *error = "replay serverbound observation limit reached";
        return false;
    }
    serverbound_observed_.push_back(frame);
    if (config_.require_serverbound_match) {
        while (serverbound_expected_cursor_ < frames_.size() &&
               frames_[serverbound_expected_cursor_].frame.direction != Direction::Serverbound) {
            ++serverbound_expected_cursor_;
        }
        if (serverbound_expected_cursor_ >= frames_.size()) {
            if (error) *error = "unexpected serverbound replay frame";
            return false;
        }
        const auto& expected = frames_[serverbound_expected_cursor_].frame;
        if (expected.packet_id != frame.packet_id || expected.payload != frame.payload) {
            if (error) *error = "serverbound replay mismatch";
            return false;
        }
        ++serverbound_expected_cursor_;
        return true;
    }
    return true;
}

void ReplayTransportBackend::close() {
    state_ = TransportState::Closing;
}

bool ReplayTransportBackend::step(std::string* error) {
    if (state_ == TransportState::Failed || state_ == TransportState::Closing || state_ == TransportState::Disconnected) {
        if (error) *error = "replay backend is not active";
        return false;
    }
    while (next_frame_ < frames_.size()) {
        const RecordedFrame& recorded = frames_[next_frame_++];
        if (recorded.frame.direction == Direction::Serverbound) continue;
        if (!receive_handler_) {
            if (error) *error = "replay backend has no receive handler";
            return false;
        }
        receive_handler_(recorded.frame);
        return true;
    }
    state_ = TransportState::InGame;
    return true;
}

} // namespace bedrockai
