#include "core/session_bootstrap.hpp"

#include <cstring>

namespace bedrockai {
namespace {

void put_be32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    o.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
    o.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    o.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    o.push_back(static_cast<std::uint8_t>(v & 0xff));
}

// Little-endian field readers (Bedrock NetworkSettings body uses LE).
bool take_u16_le(const std::vector<std::uint8_t>& b, std::size_t& p, std::uint16_t& v) {
    if (p + 2 > b.size()) return false;
    const std::uint16_t low = static_cast<std::uint16_t>(b[p]);
    const std::uint16_t high = static_cast<std::uint16_t>(static_cast<std::uint16_t>(b[p + 1]) << 8);
    v = static_cast<std::uint16_t>(low | high);
    p += 2;
    return true;
}

bool take_u8(const std::vector<std::uint8_t>& b, std::size_t& p, std::uint8_t& v) {
    if (p >= b.size()) return false;
    v = b[p++];
    return true;
}

bool take_f32_le(const std::vector<std::uint8_t>& b, std::size_t& p, float& v) {
    if (p + 4 > b.size()) return false;
    std::uint32_t n = static_cast<std::uint32_t>(b[p]) |
                      (static_cast<std::uint32_t>(b[p + 1]) << 8) |
                      (static_cast<std::uint32_t>(b[p + 2]) << 16) |
                      (static_cast<std::uint32_t>(b[p + 3]) << 24);
    p += 4;
    std::memcpy(&v, &n, sizeof(v));
    return true;
}

} // namespace

Bedrock2193LoginBootstrap::Bedrock2193LoginBootstrap(LoginBootstrapConfig config)
    : config_(std::move(config)) {}

void Bedrock2193LoginBootstrap::fail(std::string* error, const char* msg) {
    last_error_ = msg ? msg : "bootstrap failed";
    if (error) *error = last_error_;
    phase_ = LoginPhase::Failed;
}

std::vector<std::uint8_t> Bedrock2193LoginBootstrap::make_request_network_settings_body() {
    // RequestNetworkSettings body for 1.26.50/2193:
    //   big-endian int32 protocol version (2193).
    // Packet ID is carried separately by Protocol2193Session / network engine.
    std::vector<std::uint8_t> body;
    body.reserve(4);
    put_be32(body, ProtocolIdentity::kProtocol);
    return body;
}

bool Bedrock2193LoginBootstrap::start(Protocol2193Session& session, std::string* error) {
    if (phase_ != LoginPhase::Idle && phase_ != LoginPhase::TransportConnected) {
        fail(error, "bootstrap already started or in terminal state");
        return false;
    }
    if (!config_.expected.is_target()) {
        fail(error, "bootstrap requires Bedrock 1.26.50 / 2193");
        return false;
    }
    const auto st = session.state();
    if (st != TransportState::Authenticating && st != TransportState::Connecting) {
        fail(error, "session is not ready for login bootstrap");
        return false;
    }

    auto body = make_request_network_settings_body();
    if (!session.queue_serverbound(packet_id::RequestNetworkSettings, std::move(body), error)) {
        fail(error, error && !error->empty() ? error->c_str() : "failed to queue RequestNetworkSettings");
        return false;
    }
    if (!session.flush(error)) {
        fail(error, error && !error->empty() ? error->c_str() : "failed to flush RequestNetworkSettings");
        return false;
    }
    phase_ = LoginPhase::NetworkSettingsRequested;
    last_error_.clear();
    return true;
}

bool Bedrock2193LoginBootstrap::start(Protocol2193NetworkEngine& network, std::string* error) {
    if (phase_ != LoginPhase::Idle && phase_ != LoginPhase::TransportConnected) {
        fail(error, "bootstrap already started or in terminal state");
        return false;
    }
    if (!config_.expected.is_target()) {
        fail(error, "bootstrap requires Bedrock 1.26.50 / 2193");
        return false;
    }
    const auto st = network.state();
    if (st != TransportState::Authenticating && st != TransportState::Connecting && st != TransportState::InGame) {
        fail(error, "network engine is not ready for login bootstrap");
        return false;
    }
    if (!network.queue_game_packet(packet_id::RequestNetworkSettings,
                                   {static_cast<std::uint8_t>((ProtocolIdentity::kProtocol >> 24) & 0xffu),
                                    static_cast<std::uint8_t>((ProtocolIdentity::kProtocol >> 16) & 0xffu),
                                    static_cast<std::uint8_t>((ProtocolIdentity::kProtocol >> 8) & 0xffu),
                                    static_cast<std::uint8_t>(ProtocolIdentity::kProtocol & 0xffu)}, error) ||
        !network.flush_batch(error)) {
        fail(error, error && !error->empty() ? error->c_str() : "failed to send RequestNetworkSettings batch");
        return false;
    }
    phase_ = LoginPhase::NetworkSettingsRequested;
    last_error_.clear();
    return true;
}

bool Bedrock2193LoginBootstrap::parse_network_settings(const std::vector<std::uint8_t>& payload,
                                                        NetworkSettingsInfo& out,
                                                        std::string* error) {
    if (payload.size() < config_.network_settings_min_bytes) {
        if (error) *error = "NetworkSettings payload shorter than minimum for 1.26.50";
        return false;
    }
    std::size_t p = 0;
    std::uint8_t throttle = 0;
    if (!take_u16_le(payload, p, out.compression_threshold) ||
        !take_u16_le(payload, p, out.compression_algorithm) ||
        !take_u8(payload, p, throttle) ||
        !take_u8(payload, p, out.client_throttle_threshold) ||
        !take_f32_le(payload, p, out.client_throttle_scalar)) {
        if (error) *error = "malformed NetworkSettings payload";
        return false;
    }
    out.client_throttle = throttle != 0;
    // Trailing bytes (if any) are ignored intentionally for forward compatibility.
    return true;
}

bool Bedrock2193LoginBootstrap::handle_clientbound(const TransportFrame& frame, std::string* error) {
    if (phase_ == LoginPhase::Failed || phase_ == LoginPhase::Closed) {
        if (error) *error = "bootstrap already terminal";
        return false;
    }
    if (frame.direction != Direction::Clientbound) {
        fail(error, "bootstrap expects clientbound frames only");
        return false;
    }
    if (!frame.identity.is_target()) {
        fail(error, "clientbound frame protocol is not Bedrock 1.26.50 / 2193");
        return false;
    }

    if (frame.packet_id == packet_id::Disconnect) {
        phase_ = LoginPhase::Closed;
        last_error_ = "server disconnected during bootstrap";
        if (error) *error = last_error_;
        return false;
    }

    // ServerToClientHandshake is valid only immediately after Login. The secure runtime
    // consumes and verifies it before enabling packet encryption.
    if (frame.packet_id == packet_id::ServerToClientHandshake) {
        if (phase_ != LoginPhase::EncryptionNegotiating) {
            fail(error, "unexpected ServerToClientHandshake packet (wrong phase)");
            return false;
        }
        return true;
    }

    if (frame.packet_id == packet_id::NetworkSettings) {
        if (phase_ != LoginPhase::NetworkSettingsRequested) {
            fail(error, "unexpected NetworkSettings packet (wrong phase)");
            return false;
        }
        if (!parse_network_settings(frame.payload, settings_, error)) {
            fail(error, error && !error->empty() ? error->c_str() : "NetworkSettings parse failed");
            return false;
        }
        // 0xffff = disabled; 0 = raw DEFLATE in this implementation.
        // Snappy is deliberately rejected until a native codec is added; silently
        // emitting an uncompressed marker would violate the negotiated wire format.
        if (settings_.compression_algorithm != 0xffff && settings_.compression_algorithm != 0) {
            fail(error, "unsupported Bedrock compression algorithm");
            return false;
        }
        compression_enabled_ = settings_.compression_algorithm != 0xffff;
        phase_ = LoginPhase::NetworkSettingsReceived;
        last_error_.clear();
        return true;
    }

    // Other clientbound packets during bootstrap are ignored (PlayStatus, etc.).
    return true;
}

bool Bedrock2193LoginBootstrap::provide_login_payload(Protocol2193Session& session,
                                                       std::vector<std::uint8_t> login_payload,
                                                       std::string* error) {
    if (phase_ != LoginPhase::NetworkSettingsReceived) {
        fail(error, "NetworkSettings must be received before Login");
        return false;
    }
    if (login_payload.empty()) {
        fail(error, "login payload is empty");
        return false;
    }
    if (login_payload.size() > config_.max_login_payload) {
        fail(error, "login payload exceeds configured size limit");
        return false;
    }
    if (!session.queue_serverbound(packet_id::Login, std::move(login_payload), error) ||
        !session.flush(error)) {
        fail(error, error && !error->empty() ? error->c_str() : "failed to send Login");
        return false;
    }
    phase_ = config_.require_encryption ? LoginPhase::EncryptionNegotiating : LoginPhase::Authenticated;
    if (!config_.require_encryption) {
        // Skip encryption phase when explicitly disabled (test / offline modes).
        phase_ = LoginPhase::Authenticated;
    }
    last_error_.clear();
    return true;
}

bool Bedrock2193LoginBootstrap::provide_login_payload(Protocol2193NetworkEngine& network,
                                                       std::vector<std::uint8_t> login_payload,
                                                       std::string* error) {
    if (phase_ != LoginPhase::NetworkSettingsReceived) {
        fail(error, "NetworkSettings must be received before Login");
        return false;
    }
    if (login_payload.empty()) {
        fail(error, "login payload is empty");
        return false;
    }
    if (login_payload.size() > config_.max_login_payload) {
        fail(error, "login payload exceeds configured size limit");
        return false;
    }
    if (!network.queue_game_packet(packet_id::Login, std::move(login_payload), error) ||
        !network.flush_batch(error)) {
        fail(error, error && !error->empty() ? error->c_str() : "failed to send Login batch");
        return false;
    }
    phase_ = config_.require_encryption ? LoginPhase::EncryptionNegotiating : LoginPhase::Authenticated;
    last_error_.clear();
    return true;
}

bool Bedrock2193LoginBootstrap::mark_encryption_established(std::string* error) {
    if (phase_ != LoginPhase::EncryptionNegotiating) {
        if (error) *error = "encryption establishment is not currently expected";
        return false;
    }
    phase_ = LoginPhase::Authenticated;
    last_error_.clear();
    return true;
}

bool Bedrock2193LoginBootstrap::mark_world_ready(std::string* error) {
    if (phase_ != LoginPhase::Authenticated) {
        if (error) *error = "world cannot become ready before authentication";
        return false;
    }
    phase_ = LoginPhase::WorldReady;
    last_error_.clear();
    return true;
}

void Bedrock2193LoginBootstrap::close() {
    phase_ = LoginPhase::Closed;
}

} // namespace bedrockai
