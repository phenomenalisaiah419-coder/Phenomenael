#pragma once

#include "transport.hpp"
#include "raknet_reliability.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bedrockai {
namespace raknet {

// Bedrock uses a RakNet-derived UDP transport (commonly called RakNet v10 in the ecosystem).
// This module implements the connection state machine and offline-message framing.
// Actual UDP send/recv is injected via DatagramIo so the controller stays portable
// and live sockets are a host concern.

// Offline message IDs used by Bedrock's RakNet handshake.
namespace offline {
constexpr std::uint8_t UnconnectedPing = 0x01;
constexpr std::uint8_t UnconnectedPong = 0x1c;
constexpr std::uint8_t OpenConnectionRequest1 = 0x05;
constexpr std::uint8_t OpenConnectionReply1 = 0x06;
constexpr std::uint8_t OpenConnectionRequest2 = 0x07;
constexpr std::uint8_t OpenConnectionReply2 = 0x08;
constexpr std::uint8_t ConnectionRequest = 0x09;
constexpr std::uint8_t ConnectionRequestAccepted = 0x10;
constexpr std::uint8_t NewIncomingConnection = 0x13;
constexpr std::uint8_t ConnectedPing = 0x00;
constexpr std::uint8_t ConnectedPong = 0x03;
constexpr std::uint8_t DisconnectNotification = 0x15;
}

// Magic bytes used by RakNet offline messages (16 bytes).
inline const std::array<std::uint8_t, 16>& offline_magic() {
    static const std::array<std::uint8_t, 16> m = {
        0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
        0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78
    };
    return m;
}

enum class RakNetPhase {
    Idle,
    Resolving,
    OpenConnection1Sent,
    OpenConnection2Sent,
    ConnectionRequestSent,
    Connected,
    Failed,
    Closed
};

struct RakNetEndpoint {
    std::string host;
    std::uint16_t port = 19132;
};

struct RakNetConfig {
    ProtocolIdentity identity{
        ProtocolIdentity::kGameVersion,
        ProtocolIdentity::kProtocol,
        BedrockTransport::RakNet
    };
    RakNetEndpoint endpoint{};
    std::uint16_t mtu = 1492;
    // Offline RakNet negotiation may run on a mobile frame loop. Timeouts and
    // retransmission must therefore be wall-clock based, not poll-count based.
    std::uint16_t minimum_probe_mtu = 576;
    std::uint16_t mtu_probe_step = 100;
    std::uint64_t handshake_timeout_ms = 15000;
    std::uint64_t handshake_retry_interval_ms = 500;
    // Handle the RakNet Reply1 server-security cookie challenge. This is enabled
    // for the Bedrock profile. It is distinct from legacy RakNet/libcat packet
    // encryption: vanilla Bedrock clients advertise that legacy feature as false
    // and enable Minecraft-layer packet encryption after ServerToClientHandshake.
    bool support_server_security_cookie = true;
    std::uint64_t client_guid = 0; // 0 = generate stable random on connect
    std::size_t max_datagram_bytes = 2048;
    ReliabilityConfig reliability{};
};

// Injected UDP I/O. Live implementations own the socket.
class DatagramIo {
public:
    virtual ~DatagramIo() = default;
    virtual bool open(const RakNetEndpoint& endpoint, std::string* error) = 0;
    virtual bool send_to(const std::vector<std::uint8_t>& bytes, std::string* error) = 0;
    // Non-blocking or timed receive; returns nullopt on timeout/no data.
    virtual std::optional<std::vector<std::uint8_t>> recv_from(std::string* error) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;

    // Bedrock RakNet address encoding for the connected handshake. Live socket implementations
    // return the resolved peer and local addresses; test transports may return deterministic values.
    virtual std::optional<std::vector<std::uint8_t>> protocol_peer_address() const { return std::nullopt; }
    virtual std::optional<std::vector<std::uint8_t>> protocol_local_address() const { return std::nullopt; }
};

// In-memory loopback IO for unit tests (no real network).
class LoopbackDatagramIo : public DatagramIo {
public:
    bool open(const RakNetEndpoint& endpoint, std::string* error) override;
    bool send_to(const std::vector<std::uint8_t>& bytes, std::string* error) override;
    std::optional<std::vector<std::uint8_t>> recv_from(std::string* error) override;
    void close() override;
    bool is_open() const override { return open_; }
    std::optional<std::vector<std::uint8_t>> protocol_peer_address() const override;
    std::optional<std::vector<std::uint8_t>> protocol_local_address() const override;

    // Test helpers: inject a server-side datagram the client should "receive".
    void inject_inbound(std::vector<std::uint8_t> bytes);
    const std::vector<std::vector<std::uint8_t>>& outbound() const { return outbound_; }
    void clear_outbound() { outbound_.clear(); }

private:
    bool open_ = false;
    RakNetEndpoint endpoint_{};
    std::vector<std::vector<std::uint8_t>> outbound_;
    std::vector<std::vector<std::uint8_t>> inbound_;
};

// Builds offline handshake datagrams (client → server).
class OfflineMessageCodec {
public:
    static std::vector<std::uint8_t> open_connection_request1(std::uint16_t mtu);
    static std::vector<std::uint8_t> open_connection_request2(
        std::uint16_t mtu,
        std::uint64_t client_guid,
        const RakNetEndpoint& server,
        const std::optional<std::vector<std::uint8_t>>& protocol_server_address = std::nullopt,
        const std::optional<std::uint32_t>& server_security_cookie = std::nullopt);
    static std::vector<std::uint8_t> connection_request(std::uint64_t client_guid,
                                                       std::uint64_t timestamp_ms);

    // Parse helpers (return false on malformed).
    static bool is_offline_magic(const std::vector<std::uint8_t>& bytes, std::size_t offset = 1);
    static bool parse_open_connection_reply1(const std::vector<std::uint8_t>& bytes,
                                             std::uint64_t& server_guid,
                                             bool& security,
                                             std::uint16_t& mtu,
                                             std::string* error,
                                             std::uint32_t* server_security_cookie = nullptr);
    static bool parse_open_connection_reply2(const std::vector<std::uint8_t>& bytes,
                                             std::uint64_t& server_guid,
                                             std::uint16_t& mtu,
                                             std::string* error);
};

// RakNet offline handshake state machine; connected reliability is owned by RakNetReliabilityEngine.
class RakNetConnection {
public:
    explicit RakNetConnection(RakNetConfig config = {});

    void set_io(DatagramIo* io) { io_ = io; }

    bool start(std::string* error = nullptr);
    // Drive the machine with one inbound datagram (or timeout tick with empty).
    bool on_datagram(const std::vector<std::uint8_t>& bytes, std::string* error = nullptr);
    bool tick(std::string* error = nullptr); // handshake timeout handling
    // Deterministic clock seam for timing tests; production callers use tick().
    bool tick_at(std::uint64_t now_ms, std::string* error = nullptr);
    std::vector<std::uint8_t> make_connection_request();
    bool transition_connected();

    void close();

    RakNetPhase phase() const { return phase_; }
    TransportState transport_state() const;
    std::uint16_t negotiated_mtu() const { return mtu_; }
    std::uint64_t client_guid() const { return client_guid_; }
    std::uint64_t server_guid() const { return server_guid_; }
    const std::string& last_error() const { return last_error_; }
    bool is_connected() const { return phase_ == RakNetPhase::Connected; }

private:
    RakNetConfig config_;
    DatagramIo* io_ = nullptr;
    RakNetPhase phase_ = RakNetPhase::Idle;
    std::uint16_t mtu_ = 1492;
    std::uint64_t client_guid_ = 0;
    std::uint64_t server_guid_ = 0;
    std::uint64_t connection_request_timestamp_ms_ = 0;
    std::string last_error_;
    std::uint64_t phase_started_ms_ = 0;
    std::uint64_t last_probe_ms_ = 0;
    std::uint16_t probe_mtu_ = 1492;
    std::optional<std::uint32_t> server_security_cookie_;
    bool connection_request_created_ = false;

    void fail(std::string* error, const char* msg);
    bool send_bytes(const std::vector<std::uint8_t>& bytes, std::string* error);
    static std::uint64_t generate_guid();
};

struct ConnectionAcceptedInfo {
    std::uint64_t request_timestamp_ms = 0;
    std::uint64_t accepted_timestamp_ms = 0;
};

class ConnectedControlCodec {
public:
    static bool parse_connection_request_accepted(const std::vector<std::uint8_t>& payload,
                                                  ConnectionAcceptedInfo& out,
                                                  std::string* error = nullptr);
    static std::vector<std::uint8_t> new_incoming_connection(
        const std::vector<std::uint8_t>& server_address,
        const std::vector<std::uint8_t>& local_address,
        const ConnectionAcceptedInfo& accepted,
        std::string* error = nullptr);
    static std::vector<std::uint8_t> connected_ping(std::uint64_t timestamp_ms);
    static std::vector<std::uint8_t> connected_pong(std::uint64_t ping_timestamp_ms,
                                                    std::uint64_t timestamp_ms);
};

// BedrockTransportBackend adapter over RakNetConnection.
// Game packets (MCPE batches starting with 0xfe) are accepted only after Connected.
class RakNetTransportBackend : public BedrockTransportBackend {
public:
    explicit RakNetTransportBackend(RakNetConfig config = {});

    void set_io(DatagramIo* io);

    bool connect(const ProtocolIdentity& identity, std::string* error = nullptr) override;
    bool send(const TransportFrame& frame, std::string* error = nullptr) override;
    void close() override;
    TransportState state() const override;
    void set_receive_handler(ReceiveHandler handler) override;

    // Drive handshake / receive path (call from host poll loop).
    bool poll(std::string* error = nullptr);

    RakNetConnection& connection() { return conn_; }
    const RakNetConnection& connection() const { return conn_; }
    // Preserves the low-level phase or reliability error even when a host polls
    // without supplying an output error buffer (as the Android JNI bridge does).
    const std::string& last_error() const;

private:
    RakNetConfig config_;
    RakNetConnection conn_;
    DatagramIo* io_ = nullptr;
    ReceiveHandler handler_;
    TransportState state_ = TransportState::Disconnected;
    std::vector<TransportFrame> pending_game_;
    RakNetReliabilityEngine reliability_;
    bool connection_request_queued_ = false;

    bool ensure_connected(std::string* error);
    bool flush_reliability(std::string* error);
    bool handle_connected_payload(const ReceivedPayload& payload, std::string* error);
    bool maybe_queue_connection_request(std::string* error);
};

} // namespace raknet
} // namespace bedrockai
