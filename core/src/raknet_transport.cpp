#include "core/raknet_transport.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

namespace bedrockai {
namespace raknet {
namespace {

constexpr std::uint8_t kIncompatibleProtocol = 0x19;
constexpr std::uint8_t kConnectionLost = 0x16;
constexpr std::uint8_t kConnectionAttemptFailed = 0x11;

void put_be16(std::vector<std::uint8_t>& o, std::uint16_t v) {
    o.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    o.push_back(static_cast<std::uint8_t>(v & 0xff));
}
void put_be32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    o.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xffU));
    o.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xffU));
    o.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xffU));
    o.push_back(static_cast<std::uint8_t>(v & 0xffU));
}
void put_be64(std::vector<std::uint8_t>& o, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) o.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}
bool take_be16(const std::vector<std::uint8_t>& b, std::size_t& p, std::uint16_t& v) {
    if (p + 2 > b.size()) return false;
    v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[p]) << 8) | b[p + 1]);
    p += 2;
    return true;
}

bool take_be64(const std::vector<std::uint8_t>& b, std::size_t& p, std::uint64_t& v) {
    if (p + 8 > b.size()) return false;
    v = 0;
    for (std::size_t i = 0; i < 8; ++i) v = (v << 8) | b[p + i];
    p += 8;
    return true;
}

bool skip_protocol_address(const std::vector<std::uint8_t>& b, std::size_t& p) {
    if (p >= b.size()) return false;
    const auto family = b[p++];
    if (family == 0x04) return p + 4 + 2 <= b.size() ? (p += 6, true) : false;
    if (family == 0x06) return p + 2 + 2 + 4 + 16 + 4 <= b.size()
                             ? (p += 28, true)
                             : false;
    return false;
}

std::uint64_t now_millis() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::uint64_t elapsed_ms(std::uint64_t now, std::uint64_t then) {
    // A backwards clock should not occur for steady_clock, but treating it as
    // zero elapsed keeps an injected/test clock from causing unsigned wrap.
    return now >= then ? now - then : 0U;
}

ReliabilityConfig normalized_reliability(const RakNetConfig& config) {
    auto r = config.reliability;
    r.mtu = config.mtu;
    r.max_datagram_bytes = std::min(config.max_datagram_bytes, static_cast<std::size_t>(config.mtu));
    return r;
}

} // namespace

// --- LoopbackDatagramIo ---

bool LoopbackDatagramIo::open(const RakNetEndpoint& endpoint, std::string* error) {
    if (open_) {
        if (error) *error = "loopback already open";
        return false;
    }
    endpoint_ = endpoint;
    open_ = true;
    outbound_.clear();
    inbound_.clear();
    return true;
}

bool LoopbackDatagramIo::send_to(const std::vector<std::uint8_t>& bytes, std::string* error) {
    if (!open_) {
        if (error) *error = "loopback not open";
        return false;
    }
    outbound_.push_back(bytes);
    return true;
}

std::optional<std::vector<std::uint8_t>> LoopbackDatagramIo::recv_from(std::string*) {
    if (!open_ || inbound_.empty()) return std::nullopt;
    auto v = std::move(inbound_.front());
    inbound_.erase(inbound_.begin());
    return v;
}

void LoopbackDatagramIo::close() {
    open_ = false;
    outbound_.clear();
    inbound_.clear();
}

void LoopbackDatagramIo::inject_inbound(std::vector<std::uint8_t> bytes) {
    inbound_.push_back(std::move(bytes));
}

std::optional<std::vector<std::uint8_t>> LoopbackDatagramIo::protocol_peer_address() const {
    if (!open_) return std::nullopt;
    std::vector<std::uint8_t> out{0x04, 127, 0, 0, 0};
    // 127.0.0.1 is encoded with each octet complemented on the RakNet wire.
    out[1] = static_cast<std::uint8_t>(~127u);
    out[2] = static_cast<std::uint8_t>(~0u);
    out[3] = static_cast<std::uint8_t>(~0u);
    out[4] = static_cast<std::uint8_t>(~1u);
    put_be16(out, endpoint_.port);
    return out;
}

std::optional<std::vector<std::uint8_t>> LoopbackDatagramIo::protocol_local_address() const {
    return protocol_peer_address();
}

// --- OfflineMessageCodec ---

std::vector<std::uint8_t> OfflineMessageCodec::open_connection_request1(std::uint16_t mtu) {
    // RakNet's request-1 probe is padded so the entire UDP payload is the requested MTU.
    // Header is: ID(1) + magic(16) + RakNet protocol version(1).
    constexpr std::size_t kHeader = 18;
    const std::size_t total = std::max<std::size_t>(mtu, kHeader);
    std::vector<std::uint8_t> out(total, 0);
    out[0] = offline::OpenConnectionRequest1;
    const auto& magic = offline_magic();
    std::copy(magic.begin(), magic.end(), out.begin() + 1);
    out[17] = 11; // Bedrock uses RakNet wire protocol version 11.
    return out;
}

std::vector<std::uint8_t> OfflineMessageCodec::open_connection_request2(
    std::uint16_t mtu,
    std::uint64_t client_guid,
    const RakNetEndpoint& server,
    const std::optional<std::vector<std::uint8_t>>& protocol_server_address,
    const std::optional<std::uint32_t>& server_security_cookie) {
    std::vector<std::uint8_t> out;
    out.reserve(64);
    out.push_back(offline::OpenConnectionRequest2);
    const auto& magic = offline_magic();
    out.insert(out.end(), magic.begin(), magic.end());
    if (server_security_cookie.has_value()) {
        // This is the RakNet cookie challenge, not libcat transport encryption.
        // Bedrock's client support flag remains false for protocol compatibility.
        put_be32(out, *server_security_cookie);
        out.push_back(0);
    }
    if (protocol_server_address.has_value() && !protocol_server_address->empty()) {
        out.insert(out.end(), protocol_server_address->begin(), protocol_server_address->end());
    } else {
        // Literal IPv4 fallback; live SocketDatagramIo normally supplies the resolved address.
        out.push_back(0x04);
        std::uint32_t ip = 0x7f000001u;
        if (server.host == "localhost" || server.host == "127.0.0.1") {
            // keep loopback
        } else {
            // Non-literal hostnames cannot be represented correctly without DNS resolution.
            // Use zero bytes only as a deterministic test fallback.
            ip = 0;
        }
        out.push_back(static_cast<std::uint8_t>(~((ip >> 24) & 0xffu)));
        out.push_back(static_cast<std::uint8_t>(~((ip >> 16) & 0xffu)));
        out.push_back(static_cast<std::uint8_t>(~((ip >> 8) & 0xffu)));
        out.push_back(static_cast<std::uint8_t>(~(ip & 0xffu)));
        put_be16(out, server.port);
    }
    put_be16(out, mtu);
    put_be64(out, client_guid);
    return out;
}

std::vector<std::uint8_t> OfflineMessageCodec::connection_request(std::uint64_t client_guid,
                                                                  std::uint64_t timestamp_ms) {
    std::vector<std::uint8_t> out;
    out.reserve(18);
    out.push_back(offline::ConnectionRequest);
    put_be64(out, client_guid);
    put_be64(out, timestamp_ms);
    out.push_back(0); // RakNet security disabled for Bedrock profile.
    return out;
}

bool OfflineMessageCodec::is_offline_magic(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    const auto& magic = offline_magic();
    if (bytes.size() < offset + magic.size()) return false;
    return std::equal(magic.begin(), magic.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

bool OfflineMessageCodec::parse_open_connection_reply1(const std::vector<std::uint8_t>& bytes,
                                                       std::uint64_t& server_guid,
                                                       bool& security,
                                                       std::uint16_t& mtu,
                                                       std::string* error,
                                                       std::uint32_t* server_security_cookie) {
    if (bytes.empty() || bytes[0] != offline::OpenConnectionReply1) {
        if (error) *error = "not OpenConnectionReply1";
        return false;
    }
    if (!is_offline_magic(bytes, 1)) {
        if (error) *error = "OpenConnectionReply1 bad magic";
        return false;
    }
    std::size_t p = 17;
    if (!take_be64(bytes, p, server_guid)) {
        if (error) *error = "OpenConnectionReply1 truncated guid";
        return false;
    }
    if (p >= bytes.size()) {
        if (error) *error = "OpenConnectionReply1 truncated security";
        return false;
    }
    security = bytes[p++] != 0;
    if (security) {
        std::uint32_t cookie = 0;
        if (p + 4U > bytes.size()) {
            if (error) *error = "OpenConnectionReply1 security cookie truncated";
            return false;
        }
        for (int i = 0; i < 4; ++i) cookie = (cookie << 8U) | bytes[p++];
        if (server_security_cookie) *server_security_cookie = cookie;
    } else if (server_security_cookie) {
        *server_security_cookie = 0U;
    }
    if (!take_be16(bytes, p, mtu)) {
        if (error) *error = "OpenConnectionReply1 truncated mtu";
        return false;
    }
    return true;
}

bool OfflineMessageCodec::parse_open_connection_reply2(const std::vector<std::uint8_t>& bytes,
                                                       std::uint64_t& server_guid,
                                                       std::uint16_t& mtu,
                                                       std::string* error) {
    if (bytes.empty() || bytes[0] != offline::OpenConnectionReply2) {
        if (error) *error = "not OpenConnectionReply2";
        return false;
    }
    if (!is_offline_magic(bytes, 1)) {
        if (error) *error = "OpenConnectionReply2 bad magic";
        return false;
    }
    std::size_t p = 17;
    if (!take_be64(bytes, p, server_guid)) {
        if (error) *error = "OpenConnectionReply2 truncated guid";
        return false;
    }
    if (!skip_protocol_address(bytes, p)) {
        if (error) *error = "OpenConnectionReply2 truncated address";
        return false;
    }
    if (!take_be16(bytes, p, mtu)) {
        if (error) *error = "OpenConnectionReply2 truncated mtu";
        return false;
    }
    if (p >= bytes.size()) {
        if (error) *error = "OpenConnectionReply2 missing encryption flag";
        return false;
    }
    if (bytes[p] != 0) {
        if (error) *error = "RakNet encryption-enabled server is outside the Bedrock profile";
        return false;
    }
    return true;
}

// --- RakNetConnection ---

std::uint64_t RakNetConnection::generate_guid() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dist;
    std::uint64_t v = dist(gen);
    return v == 0 ? 1 : v;
}

RakNetConnection::RakNetConnection(RakNetConfig config) : config_(std::move(config)) {
    mtu_ = config_.mtu;
    client_guid_ = config_.client_guid;
    probe_mtu_ = config_.mtu;
}

void RakNetConnection::fail(std::string* error, const char* msg) {
    last_error_ = msg ? msg : "raknet failed";
    if (error) *error = last_error_;
    phase_ = RakNetPhase::Failed;
}

bool RakNetConnection::send_bytes(const std::vector<std::uint8_t>& bytes, std::string* error) {
    if (!io_ || !io_->is_open()) {
        fail(error, "no datagram IO");
        return false;
    }
    if (bytes.size() > config_.max_datagram_bytes) {
        fail(error, "handshake datagram exceeds configured limit");
        return false;
    }
    if (!io_->send_to(bytes, error)) {
        fail(error, error && !error->empty() ? error->c_str() : "datagram send failed");
        return false;
    }
    return true;
}

TransportState RakNetConnection::transport_state() const {
    switch (phase_) {
        case RakNetPhase::Idle: return TransportState::Disconnected;
        case RakNetPhase::Resolving:
        case RakNetPhase::OpenConnection1Sent:
        case RakNetPhase::OpenConnection2Sent:
        case RakNetPhase::ConnectionRequestSent: return TransportState::Connecting;
        case RakNetPhase::Connected: return TransportState::Authenticating;
        case RakNetPhase::Failed: return TransportState::Failed;
        case RakNetPhase::Closed: return TransportState::Closing;
    }
    return TransportState::Failed;
}

bool RakNetConnection::start(std::string* error) {
    if (phase_ != RakNetPhase::Idle && phase_ != RakNetPhase::Closed) {
        fail(error, "raknet connection already started");
        return false;
    }
    if (!config_.identity.is_target()) {
        fail(error, "raknet requires Bedrock 1.26.50 / 2193");
        return false;
    }
    if (!io_) {
        fail(error, "raknet has no DatagramIo");
        return false;
    }
    if (!io_->open(config_.endpoint, error)) {
        fail(error, error && !error->empty() ? error->c_str() : "datagram open failed");
        return false;
    }
    if (client_guid_ == 0) client_guid_ = generate_guid();
    mtu_ = config_.mtu;
    probe_mtu_ = config_.mtu;
    server_security_cookie_.reset();
    phase_started_ms_ = now_millis();
    last_probe_ms_ = phase_started_ms_;
    connection_request_created_ = false;
    phase_ = RakNetPhase::OpenConnection1Sent;
    auto req1 = OfflineMessageCodec::open_connection_request1(probe_mtu_);
    if (!send_bytes(req1, error)) return false;
    last_error_.clear();
    return true;
}

bool RakNetConnection::on_datagram(const std::vector<std::uint8_t>& bytes, std::string* error) {
    if (bytes.empty()) return true;
    if (phase_ == RakNetPhase::Failed || phase_ == RakNetPhase::Closed) {
        if (error) *error = "raknet already terminal";
        return false;
    }

    const std::uint8_t id = bytes[0];
    if (id == offline::DisconnectNotification || id == kConnectionLost) {
        phase_ = RakNetPhase::Closed;
        last_error_ = "server disconnect notification";
        if (error) *error = last_error_;
        return false;
    }
    if (id == kIncompatibleProtocol || id == kConnectionAttemptFailed) {
        fail(error, "server rejected RakNet connection attempt");
        return false;
    }

    if (phase_ == RakNetPhase::OpenConnection1Sent && id == offline::OpenConnectionReply1) {
        bool security = false;
        std::uint16_t mtu = mtu_;
        std::uint32_t server_cookie = 0U;
        if (!OfflineMessageCodec::parse_open_connection_reply1(
                bytes, server_guid_, security, mtu, error, &server_cookie)) {
            fail(error, error && !error->empty() ? error->c_str() : "bad reply1");
            return false;
        }
        if (security && !config_.support_server_security_cookie) {
            fail(error, "server requires the RakNet cookie challenge but support is disabled");
            return false;
        }
        if (mtu < 400 || mtu > config_.mtu) {
            fail(error, "server returned an invalid negotiated MTU");
            return false;
        }
        mtu_ = mtu;
        server_security_cookie_ = security ? std::optional<std::uint32_t>(server_cookie) : std::nullopt;
        auto address = io_->protocol_peer_address();
        auto req2 = OfflineMessageCodec::open_connection_request2(
            mtu_, client_guid_, config_.endpoint, address, server_security_cookie_);
        if (!send_bytes(req2, error)) return false;
        phase_ = RakNetPhase::OpenConnection2Sent;
        phase_started_ms_ = now_millis();
        last_probe_ms_ = phase_started_ms_;
        return true;
    }

    if (phase_ == RakNetPhase::OpenConnection2Sent && id == offline::OpenConnectionReply2) {
        std::uint16_t mtu = mtu_;
        if (!OfflineMessageCodec::parse_open_connection_reply2(bytes, server_guid_, mtu, error)) {
            fail(error, error && !error->empty() ? error->c_str() : "bad reply2");
            return false;
        }
        if (mtu < 400 || mtu > config_.mtu) {
            fail(error, "server returned an invalid final MTU");
            return false;
        }
        mtu_ = mtu;
        phase_ = RakNetPhase::ConnectionRequestSent;
        phase_started_ms_ = now_millis();
        last_probe_ms_ = phase_started_ms_;
        return true;
    }

    // Connected datagrams, including ConnectionRequestAccepted, are decoded by the reliability engine.
    return true;
}

std::vector<std::uint8_t> RakNetConnection::make_connection_request() {
    if (connection_request_created_) return {};
    connection_request_timestamp_ms_ = now_millis();
    connection_request_created_ = true;
    return OfflineMessageCodec::connection_request(client_guid_, connection_request_timestamp_ms_);
}

bool RakNetConnection::transition_connected() {
    if (phase_ != RakNetPhase::ConnectionRequestSent && phase_ != RakNetPhase::Connected) return false;
    phase_ = RakNetPhase::Connected;
    phase_started_ms_ = now_millis();
    last_probe_ms_ = phase_started_ms_;
    last_error_.clear();
    return true;
}

bool RakNetConnection::tick(std::string* error) {
    return tick_at(now_millis(), error);
}

bool RakNetConnection::tick_at(std::uint64_t now_ms, std::string* error) {
    if (phase_ == RakNetPhase::Failed || phase_ == RakNetPhase::Closed ||
        phase_ == RakNetPhase::Connected || phase_ == RakNetPhase::Idle) {
        return phase_ != RakNetPhase::Failed;
    }
    if (elapsed_ms(now_ms, phase_started_ms_) >= config_.handshake_timeout_ms) {
        const char* stage = "unknown stage";
        switch (phase_) {
            case RakNetPhase::Resolving: stage = "resolving"; break;
            case RakNetPhase::OpenConnection1Sent: stage = "waiting for OpenConnectionReply1"; break;
            case RakNetPhase::OpenConnection2Sent: stage = "waiting for OpenConnectionReply2"; break;
            case RakNetPhase::ConnectionRequestSent: stage = "waiting for ConnectionRequestAccepted"; break;
            case RakNetPhase::Idle: stage = "idle"; break;
            case RakNetPhase::Connected: stage = "connected"; break;
            case RakNetPhase::Failed: stage = "failed"; break;
            case RakNetPhase::Closed: stage = "closed"; break;
        }
        fail(error, (std::string("RakNet handshake timeout: ") + stage).c_str());
        return false;
    }
    if (elapsed_ms(now_ms, last_probe_ms_) >= config_.handshake_retry_interval_ms) {
        if (phase_ == RakNetPhase::OpenConnection1Sent) {
            const auto min_mtu = std::max<std::uint16_t>(400U, std::min(config_.minimum_probe_mtu, config_.mtu));
            if (probe_mtu_ > min_mtu) {
                const auto step = std::max<std::uint16_t>(1U, config_.mtu_probe_step);
                const auto next = probe_mtu_ > static_cast<std::uint16_t>(min_mtu + step)
                                      ? static_cast<std::uint16_t>(probe_mtu_ - step)
                                      : min_mtu;
                probe_mtu_ = next;
            }
            last_probe_ms_ = now_ms;
            return send_bytes(OfflineMessageCodec::open_connection_request1(probe_mtu_), error);
        }
        if (phase_ == RakNetPhase::OpenConnection2Sent) {
            auto address = io_ ? io_->protocol_peer_address() : std::nullopt;
            last_probe_ms_ = now_ms;
            return send_bytes(OfflineMessageCodec::open_connection_request2(
                                  mtu_, client_guid_, config_.endpoint, address, server_security_cookie_), error);
        }
        // ConnectionRequest is reliable now; retransmission is owned by the reliability engine.
        last_probe_ms_ = now_ms;
    }
    return true;
}

void RakNetConnection::close() {
    if (io_ && io_->is_open()) io_->close();
    phase_ = RakNetPhase::Closed;
}

// --- Connected control codec ---

bool ConnectedControlCodec::parse_connection_request_accepted(
    const std::vector<std::uint8_t>& payload,
    ConnectionAcceptedInfo& out,
    std::string* error) {
    if (payload.size() < 1 || payload[0] != offline::ConnectionRequestAccepted) {
        if (error) *error = "not ConnectionRequestAccepted";
        return false;
    }
    std::size_t p = 1;
    if (!skip_protocol_address(payload, p)) {
        if (error) *error = "ConnectionRequestAccepted client address truncated";
        return false;
    }
    std::uint16_t system_index = 0;
    if (!take_be16(payload, p, system_index)) {
        if (error) *error = "ConnectionRequestAccepted system index truncated";
        return false;
    }
    (void)system_index;
    for (int i = 0; i < 10; ++i) {
        if (!skip_protocol_address(payload, p)) {
            if (error) *error = "ConnectionRequestAccepted local address list truncated";
            return false;
        }
    }
    if (!take_be64(payload, p, out.request_timestamp_ms) ||
        !take_be64(payload, p, out.accepted_timestamp_ms)) {
        if (error) *error = "ConnectionRequestAccepted timestamps truncated";
        return false;
    }
    return p == payload.size();
}

std::vector<std::uint8_t> ConnectedControlCodec::new_incoming_connection(
    const std::vector<std::uint8_t>& server_address,
    const std::vector<std::uint8_t>& local_address,
    const ConnectionAcceptedInfo& accepted,
    std::string* error) {
    if (server_address.empty() || local_address.empty()) {
        if (error) *error = "NewIncomingConnection requires server and local addresses";
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(1 + server_address.size() + local_address.size() * 10 + 16);
    out.push_back(offline::NewIncomingConnection);
    out.insert(out.end(), server_address.begin(), server_address.end());
    for (int i = 0; i < 10; ++i) out.insert(out.end(), local_address.begin(), local_address.end());
    put_be64(out, accepted.accepted_timestamp_ms);
    put_be64(out, now_millis());
    return out;
}

std::vector<std::uint8_t> ConnectedControlCodec::connected_ping(std::uint64_t timestamp_ms) {
    std::vector<std::uint8_t> out;
    out.reserve(9);
    out.push_back(offline::ConnectedPing);
    put_be64(out, timestamp_ms);
    return out;
}

std::vector<std::uint8_t> ConnectedControlCodec::connected_pong(std::uint64_t ping_timestamp_ms,
                                                                std::uint64_t timestamp_ms) {
    std::vector<std::uint8_t> out;
    out.reserve(17);
    out.push_back(offline::ConnectedPong);
    put_be64(out, ping_timestamp_ms);
    put_be64(out, timestamp_ms);
    return out;
}

// --- RakNetTransportBackend ---

RakNetTransportBackend::RakNetTransportBackend(RakNetConfig config)
    : config_(std::move(config)),
      conn_(config_),
      reliability_(normalized_reliability(config_)) {}

void RakNetTransportBackend::set_io(DatagramIo* io) {
    io_ = io;
    conn_.set_io(io);
}

bool RakNetTransportBackend::connect(const ProtocolIdentity& identity, std::string* error) {
    if (!identity.is_target()) {
        if (error) *error = "RakNet backend requires Bedrock 1.26.50 / 2193";
        state_ = TransportState::Failed;
        return false;
    }
    if (!io_) {
        if (error) *error = "RakNet backend has no DatagramIo";
        state_ = TransportState::Failed;
        return false;
    }
    config_.identity = identity;
    config_.identity.transport = BedrockTransport::RakNet;
    config_.reliability = normalized_reliability(config_);
    reliability_ = RakNetReliabilityEngine(config_.reliability);
    pending_game_.clear();
    connection_request_queued_ = false;
    conn_ = RakNetConnection(config_);
    conn_.set_io(io_);
    if (!conn_.start(error)) {
        state_ = TransportState::Failed;
        return false;
    }
    state_ = TransportState::Connecting;
    return true;
}

bool RakNetTransportBackend::ensure_connected(std::string* error) {
    if (conn_.is_connected()) {
        state_ = TransportState::Authenticating;
        return true;
    }
    if (conn_.phase() == RakNetPhase::Failed || reliability_.terminal()) {
        state_ = TransportState::Failed;
        if (error) *error = reliability_.terminal() ? reliability_.last_error() : conn_.last_error();
        return false;
    }
    if (error) *error = "raknet not connected yet";
    return false;
}

bool RakNetTransportBackend::flush_reliability(std::string* error) {
    if (!io_ || !io_->is_open()) {
        if (error) *error = "no datagram IO";
        return false;
    }
    const auto now = now_millis();
    reliability_.tick(now);
    if (reliability_.terminal()) {
        if (error) *error = reliability_.last_error();
        state_ = TransportState::Failed;
        return false;
    }
    while (auto pending = reliability_.next_outbound()) {
        if (!io_->send_to(pending->bytes, error)) {
            reliability_.requeue_front(std::move(*pending));
            return false;
        }
        reliability_.mark_sent(*pending, now_millis());
    }
    return !reliability_.terminal();
}

bool RakNetTransportBackend::maybe_queue_connection_request(std::string* error) {
    if (conn_.phase() != RakNetPhase::ConnectionRequestSent || connection_request_queued_) return true;
    auto request = conn_.make_connection_request();
    if (request.empty()) {
        if (error) *error = "failed to create ConnectionRequest";
        return false;
    }
    if (!reliability_.queue(std::move(request), RakNetReliability::ReliableOrdered, 0, error)) {
        return false;
    }
    connection_request_queued_ = true;
    return flush_reliability(error);
}

bool RakNetTransportBackend::handle_connected_payload(const ReceivedPayload& payload, std::string* error) {
    if (payload.payload.empty()) return true;
    const auto id = payload.payload[0];

    if (id == offline::ConnectionRequestAccepted) {
        ConnectionAcceptedInfo accepted;
        if (!ConnectedControlCodec::parse_connection_request_accepted(payload.payload, accepted, error)) return false;
        const auto server_address = io_ ? io_->protocol_peer_address() : std::nullopt;
        const auto local_address = io_ ? io_->protocol_local_address() : std::nullopt;
        if (!server_address || !local_address) {
            if (error) *error = "transport cannot encode NewIncomingConnection addresses";
            return false;
        }
        auto new_incoming = ConnectedControlCodec::new_incoming_connection(
            *server_address, *local_address, accepted, error);
        if (new_incoming.empty()) return false;
        if (!reliability_.queue(std::move(new_incoming), RakNetReliability::ReliableOrdered, 0, error)) {
            return false;
        }
        auto ping = ConnectedControlCodec::connected_ping(now_millis());
        if (!reliability_.queue(std::move(ping), RakNetReliability::Unreliable, 0, error)) return false;
        if (!conn_.transition_connected()) {
            if (error) *error = "ConnectionRequestAccepted arrived in the wrong RakNet phase";
            return false;
        }
        state_ = TransportState::Authenticating;
        return true;
    }

    if (id == offline::ConnectedPing) {
        if (payload.payload.size() != 9) {
            if (error) *error = "ConnectedPing has invalid size";
            return false;
        }
        std::size_t p = 1;
        std::uint64_t ping = 0;
        if (!take_be64(payload.payload, p, ping)) {
            if (error) *error = "ConnectedPing timestamp truncated";
            return false;
        }
        auto pong = ConnectedControlCodec::connected_pong(ping, now_millis());
        if (!reliability_.queue(std::move(pong), RakNetReliability::Unreliable, 0, error)) return false;
        return true;
    }

    if (id == offline::ConnectedPong) {
        if (payload.payload.size() != 17) {
            if (error) *error = "ConnectedPong has invalid size";
            return false;
        }
        return true;
    }

    if (id == offline::DisconnectNotification || id == kConnectionLost) {
        conn_.close();
        state_ = TransportState::Closing;
        if (error) *error = "server disconnected";
        return false;
    }

    if (!handler_) return true;
    TransportFrame f;
    f.identity = config_.identity;
    f.direction = Direction::Clientbound;
    f.packet_id = id;
    f.sequence = payload.datagram_sequence;
    f.payload = payload.payload;
    handler_(f);
    return true;
}

bool RakNetTransportBackend::send(const TransportFrame& frame, std::string* error) {
    if (!ensure_connected(error)) return false;
    if (!io_ || !io_->is_open()) {
        if (error) *error = "no datagram IO";
        return false;
    }
    if (frame.payload.empty()) {
        if (error) *error = "empty transport payload";
        return false;
    }
    // Bedrock game/control payloads are encapsulated as reliable ordered frames on channel 0.
    // The network engine is responsible for adding the 0xFE batch wrapper where required.
    if (!reliability_.queue(frame.payload, RakNetReliability::ReliableOrdered, 0, error)) return false;
    return flush_reliability(error);
}

void RakNetTransportBackend::close() {
    conn_.close();
    state_ = TransportState::Closing;
}

TransportState RakNetTransportBackend::state() const {
    if (conn_.is_connected()) return TransportState::Authenticating;
    if (reliability_.terminal()) return TransportState::Failed;
    return conn_.transport_state();
}

const std::string& RakNetTransportBackend::last_error() const {
    if (reliability_.terminal() && !reliability_.last_error().empty()) return reliability_.last_error();
    return conn_.last_error();
}

void RakNetTransportBackend::set_receive_handler(ReceiveHandler handler) {
    handler_ = std::move(handler);
}

bool RakNetTransportBackend::poll(std::string* error) {
    if (error) error->clear();
    if (!io_) {
        if (error) *error = "no datagram IO";
        return false;
    }

    for (;;) {
        auto pkt = io_->recv_from(error);
        if (!pkt) break;
        if (pkt->empty()) continue;

        const auto id = (*pkt)[0];
        const bool frame_set = id >= kFrameSetMin && id <= kFrameSetMax;
        const bool ack = id == kAck || id == kNack;

        if (frame_set || ack) {
            bool callback_failed = false;
            std::string callback_error;
            if (!reliability_.ingest(*pkt, now_millis(),
                                     [this, &callback_failed, &callback_error](const ReceivedPayload& payload) {
                                         std::string local_error;
                                         if (!handle_connected_payload(payload, &local_error)) {
                                             callback_failed = true;
                                             callback_error = std::move(local_error);
                                         }
                                     }, error)) {
                state_ = reliability_.terminal() ? TransportState::Failed : TransportState::Closing;
                if (reliability_.terminal() && error && error->empty()) *error = reliability_.last_error();
                return false;
            }
            if (callback_failed) {
                state_ = (conn_.phase() == RakNetPhase::Closed) ? TransportState::Closing : TransportState::Failed;
                if (error && error->empty()) *error = callback_error.empty() ? "connected payload handler rejected packet" : callback_error;
                return false;
            }
            if (reliability_.terminal()) {
                state_ = TransportState::Failed;
                if (error) *error = reliability_.last_error();
                return false;
            }
        } else if (!conn_.is_connected()) {
            if (!conn_.on_datagram(*pkt, error)) {
                state_ = (conn_.phase() == RakNetPhase::Closed) ? TransportState::Closing
                                                                 : TransportState::Failed;
                return false;
            }
        } else {
            // Bare disconnect notifications remain accepted defensively.
            if (!conn_.on_datagram(*pkt, error)) {
                state_ = (conn_.phase() == RakNetPhase::Closed) ? TransportState::Closing
                                                                 : TransportState::Failed;
                return false;
            }
        }

        if (!maybe_queue_connection_request(error)) {
            state_ = TransportState::Failed;
            return false;
        }
    }

    if (!conn_.is_connected() && conn_.phase() != RakNetPhase::Closed) {
        if (!conn_.tick(error)) {
            state_ = TransportState::Failed;
            return false;
        }
    }
    if (!flush_reliability(error)) {
        state_ = TransportState::Failed;
        return false;
    }
    return true;
}

} // namespace raknet
} // namespace bedrockai
