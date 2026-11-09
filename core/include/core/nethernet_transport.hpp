#pragma once

#include "transport.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace bedrockai {

// Host bridge for the reliable NetherNet WebRTC data channel. ICE/DTLS/SCTP
// negotiation remains owned by the platform WebRTC stack; this class provides
// the Bedrock transport-facing half without pretending to implement WebRTC itself.
class NetherNetDataChannelIo {
public:
    using SendHandler = std::function<bool(const std::vector<std::uint8_t>&, std::string*)>;
    using CloseHandler = std::function<void()>;

    void set_send_handler(SendHandler handler) { send_ = std::move(handler); }
    void set_close_handler(CloseHandler handler) { close_ = std::move(handler); }

    bool open(std::string* error = nullptr);
    bool send(const std::vector<std::uint8_t>& data, std::string* error = nullptr);
    void receive(std::vector<std::uint8_t> data);
    std::optional<std::vector<std::uint8_t>> poll();
    void close();
    bool open_state() const { std::lock_guard<std::mutex> lock(mutex_); return open_; }
    std::size_t pending() const { std::lock_guard<std::mutex> lock(mutex_); return inbound_.size(); }

private:
    bool open_ = false;
    SendHandler send_{};
    CloseHandler close_{};
    mutable std::mutex mutex_;
    std::vector<std::vector<std::uint8_t>> inbound_;
};

class NetherNetTransportBackend final : public BedrockTransportBackend {
public:
    explicit NetherNetTransportBackend(NetherNetDataChannelIo& io);

    bool connect(const ProtocolIdentity& identity, std::string* error = nullptr) override;
    bool send(const TransportFrame& frame, std::string* error = nullptr) override;
    void close() override;
    TransportState state() const override { return state_; }
    void set_receive_handler(ReceiveHandler handler) override { receive_handler_ = std::move(handler); }

    bool poll(std::string* error = nullptr);

private:
    NetherNetDataChannelIo& io_;
    TransportState state_ = TransportState::Disconnected;
    ProtocolIdentity identity_{};
    std::uint64_t sequence_ = 1;
    ReceiveHandler receive_handler_{};
};

} // namespace bedrockai
