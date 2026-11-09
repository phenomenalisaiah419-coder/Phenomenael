#pragma once

#include "recording.hpp"
#include "transport.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bedrockai {

struct ReplayBackendConfig {
    ProtocolIdentity identity{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::Unknown};
    bool auto_play = false;
    bool require_serverbound_match = false;
    std::size_t max_frames = 4096;
};

// Deterministic transport backend for recorded real sessions. It replays logical
// Bedrock frames, so a captured session can exercise the secure runtime without a socket.
class ReplayTransportBackend final : public BedrockTransportBackend {
public:
    explicit ReplayTransportBackend(ReplayBackendConfig config = {});

    bool load(const std::vector<RecordedFrame>& frames, std::string* error = nullptr);
    bool load_file(const std::string& path, std::string* error = nullptr);
    bool connect(const ProtocolIdentity& identity, std::string* error = nullptr) override;
    bool send(const TransportFrame& frame, std::string* error = nullptr) override;
    void close() override;
    TransportState state() const override { return state_; }
    void set_receive_handler(ReceiveHandler handler) override { receive_handler_ = std::move(handler); }

    bool step(std::string* error = nullptr);
    bool finished() const { return next_frame_ >= frames_.size(); }
    std::size_t cursor() const { return next_frame_; }
    const std::vector<TransportFrame>& serverbound_observed() const { return serverbound_observed_; }

private:
    ReplayBackendConfig config_{};
    TransportState state_ = TransportState::Disconnected;
    std::vector<RecordedFrame> frames_;
    std::size_t next_frame_ = 0;
    ReceiveHandler receive_handler_{};
    std::vector<TransportFrame> serverbound_observed_;
    std::size_t serverbound_expected_cursor_ = 0;
    std::size_t max_serverbound_ = 4096;
};

} // namespace bedrockai
