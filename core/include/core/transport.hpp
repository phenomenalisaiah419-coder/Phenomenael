#pragma once

#include "protocol_adapter.hpp"
#include "state.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bedrockai {

enum class TransportState {
    Disconnected,
    Connecting,
    Authenticating,
    InGame,
    Closing,
    Failed
};

struct TransportFrame {
    static constexpr std::uint32_t kMagic = 0x42414931u; // BAI1
    static constexpr std::uint16_t kFormatVersion = 1;

    std::uint32_t magic = kMagic;
    std::uint16_t format_version = kFormatVersion;
    ProtocolIdentity identity{};
    Direction direction = Direction::Clientbound;
    std::uint32_t packet_id = 0;
    std::uint64_t sequence = 0;
    std::vector<std::uint8_t> payload;
};

class TransportFrameCodec {
public:
    static std::vector<std::uint8_t> encode(const TransportFrame& frame);
    static std::optional<TransportFrame> decode(const std::vector<std::uint8_t>& bytes,
                                                std::string* error = nullptr);
};

class BedrockTransportBackend {
public:
    using ReceiveHandler = std::function<void(const TransportFrame&)>;
    virtual ~BedrockTransportBackend() = default;

    virtual bool connect(const ProtocolIdentity& identity, std::string* error = nullptr) = 0;
    virtual bool send(const TransportFrame& frame, std::string* error = nullptr) = 0;
    virtual void close() = 0;
    virtual TransportState state() const = 0;
    virtual void set_receive_handler(ReceiveHandler handler) = 0;
};

struct TransportSessionConfig {
    ProtocolIdentity expected{
        ProtocolIdentity::kGameVersion,
        ProtocolIdentity::kProtocol,
        BedrockTransport::Unknown
    };
    std::size_t max_outbound_frames = 128;
};

class Protocol2193Session {
public:
    using ReceiveHandler = std::function<void(const TransportFrame&)>;
    explicit Protocol2193Session(TransportSessionConfig config = {});

    bool attach(BedrockTransportBackend& backend, std::string* error = nullptr);
    bool queue_serverbound(std::uint32_t packet_id,
                           std::vector<std::uint8_t> payload,
                           std::string* error = nullptr);
    bool flush(std::string* error = nullptr);
    void detach();

    bool ingest(const TransportFrame& frame, std::string* error = nullptr);
    void set_receive_handler(ReceiveHandler handler) { receive_handler_ = std::move(handler); }

    const WorldSnapshot& snapshot() const { return snapshot_; }
    const ProtocolIdentity& identity() const { return identity_; }
    TransportState state() const {
        if (!backend_ || state_ == TransportState::InGame || state_ == TransportState::Closing || state_ == TransportState::Failed) {
            return state_;
        }
        const auto backend_state = backend_->state();
        // A backend may report its underlying socket as usable immediately (some test
        // backends report InGame on connect). The Bedrock session itself does not become
        // InGame until it has observed StartGame.
        return backend_state == TransportState::InGame ? TransportState::Authenticating : backend_state;
    }
    std::size_t pending_outbound() const { return outbound_.size(); }

private:
    TransportSessionConfig config_;
    BedrockTransportBackend* backend_ = nullptr;
    ProtocolIdentity identity_{};
    TransportState state_ = TransportState::Disconnected;
    std::uint64_t next_sequence_ = 1;
    std::vector<TransportFrame> outbound_;
    WorldSnapshot snapshot_{};
    ProtocolAdapter adapter_{};
    ReceiveHandler receive_handler_{};

    bool validate_frame(const TransportFrame& frame, std::string* error) const;
    void on_transport_frame(const TransportFrame& frame);
    void transition_from_packet(const TransportFrame& frame);
};

} // namespace bedrockai
