#include "core/nethernet_transport.hpp"
#include "core/transport.hpp"

#include <utility>

namespace bedrockai {

bool NetherNetDataChannelIo::open(std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_) return true;
    if (!send_) {
        if (error) *error = "NetherNet data-channel send handler is not configured";
        return false;
    }
    open_ = true;
    return true;
}

bool NetherNetDataChannelIo::send(const std::vector<std::uint8_t>& data, std::string* error) {
    SendHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_) {
            if (error) *error = "NetherNet data channel is closed";
            return false;
        }
        handler = send_;
    }
    if (!handler) {
        if (error) *error = "NetherNet send handler is not configured";
        return false;
    }
    return handler(data, error);
}

void NetherNetDataChannelIo::receive(std::vector<std::uint8_t> data) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return;
    inbound_.push_back(std::move(data));
}

std::optional<std::vector<std::uint8_t>> NetherNetDataChannelIo::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (inbound_.empty()) return std::nullopt;
    auto out = std::move(inbound_.front());
    inbound_.erase(inbound_.begin());
    return out;
}

void NetherNetDataChannelIo::close() {
    CloseHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_) return;
        open_ = false;
        handler = close_;
    }
    if (handler) handler();
}

NetherNetTransportBackend::NetherNetTransportBackend(NetherNetDataChannelIo& io) : io_(io) {}

bool NetherNetTransportBackend::connect(const ProtocolIdentity& identity, std::string* error) {
    if (!identity.is_target()) {
        if (error) *error = "NetherNet backend requires Bedrock 1.26.50 / 2193";
        state_ = TransportState::Failed;
        return false;
    }
    identity_ = identity;
    identity_.transport = BedrockTransport::NetherNet;
    if (!io_.open(error)) {
        state_ = TransportState::Failed;
        return false;
    }
    sequence_ = 1;
    state_ = TransportState::Authenticating;
    return true;
}

bool NetherNetTransportBackend::send(const TransportFrame& frame, std::string* error) {
    if (state_ != TransportState::Authenticating && state_ != TransportState::InGame) {
        if (error) *error = "NetherNet backend is not connected";
        return false;
    }
    if (frame.packet_id != 0xfeu) {
        if (error) *error = "NetherNet carries Bedrock 0xFE batch payloads only";
        return false;
    }
    if (frame.payload.empty() || frame.payload.front() != 0xfeu) {
        if (error) *error = "NetherNet batch payload must begin with 0xFE";
        return false;
    }
    return io_.send(frame.payload, error);
}

void NetherNetTransportBackend::close() {
    io_.close();
    state_ = TransportState::Closing;
}

bool NetherNetTransportBackend::poll(std::string* error) {
    auto bytes = io_.poll();
    if (!bytes) return true;
    if (bytes->empty() || bytes->front() != 0xfeu) {
        if (error) *error = "NetherNet received a non-Bedrock batch message";
        state_ = TransportState::Failed;
        return false;
    }
    TransportFrame frame;
    frame.identity = identity_;
    frame.direction = Direction::Clientbound;
    frame.packet_id = 0xfe;
    frame.sequence = sequence_++;
    frame.payload = std::move(*bytes);
    if (receive_handler_) receive_handler_(frame);
    return true;
}

} // namespace bedrockai
