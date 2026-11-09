#pragma once

#include "protocol_adapter.hpp"
#include "network_engine.hpp"
#include "transport.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace bedrockai {

enum class LoginPhase {
    Idle,
    TransportConnected,
    NetworkSettingsRequested,
    NetworkSettingsReceived,
    LoginSent,
    EncryptionNegotiating,
    Authenticated,
    WorldReady,
    Failed,
    Closed
};

struct NetworkSettingsInfo {
    std::uint16_t compression_threshold = 0;
    // 0xffff = compression disabled; 0 = zlib/deflate; 1 = snappy (if advertised).
    std::uint16_t compression_algorithm = 0xffff;
    bool client_throttle = false;
    std::uint8_t client_throttle_threshold = 0;
    float client_throttle_scalar = 0.0f;
};

struct LoginBootstrapConfig {
    ProtocolIdentity expected{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::Unknown};
    bool require_encryption = true;
    std::size_t max_login_payload = 1024 * 1024;
    // Minimum bytes required for NetworkSettings on 1.26.50/2193.
    // Layout (little-endian):
    //   u16 compression_threshold
    //   u16 compression_algorithm
    //   u8  client_throttle (0/1)
    //   u8  client_throttle_threshold
    //   f32 client_throttle_scalar
    // Extra trailing bytes are tolerated and ignored so minor server extensions
    // do not hard-fail the bootstrap.
    std::size_t network_settings_min_bytes = 10;
};

class Bedrock2193LoginBootstrap {
public:
    explicit Bedrock2193LoginBootstrap(LoginBootstrapConfig config = {});

    // Begin bootstrap: queue RequestNetworkSettings (protocol 2193, big-endian body).
    bool start(Protocol2193Session& session, std::string* error = nullptr);
    bool start(Protocol2193NetworkEngine& network, std::string* error = nullptr);

    // Feed clientbound frames. Handles NetworkSettings, Disconnect, and (when
    // present) ServerToClientHandshake without requiring the transport layer to
    // understand login semantics.
    bool handle_clientbound(const TransportFrame& frame, std::string* error = nullptr);

    // Supply the opaque Login packet payload produced by a real auth provider.
    // This boundary deliberately does not fabricate Microsoft/Xbox JWTs.
    bool provide_login_payload(Protocol2193Session& session,
                               std::vector<std::uint8_t> login_payload,
                               std::string* error = nullptr);
    bool provide_login_payload(Protocol2193NetworkEngine& network,
                               std::vector<std::uint8_t> login_payload,
                               std::string* error = nullptr);

    // Called by the encryption layer after ServerToClientHandshake has been
    // processed and the session keys are established. Until a real crypto
    // backend is wired, the host may call this after observing the handshake
    // packet; the phase machine itself remains strict.
    bool mark_encryption_established(std::string* error = nullptr);

    // Called after StartGame (or equivalent world-ready signal) has been
    // observed by the higher-level session. AI control must not start before
    // this phase.
    bool mark_world_ready(std::string* error = nullptr);

    void close();

    LoginPhase phase() const { return phase_; }
    const NetworkSettingsInfo& network_settings() const { return settings_; }
    bool compression_enabled() const { return compression_enabled_; }
    bool encryption_required() const { return config_.require_encryption; }
    const std::string& last_error() const { return last_error_; }
    bool is_terminal() const {
        return phase_ == LoginPhase::Failed || phase_ == LoginPhase::Closed || phase_ == LoginPhase::WorldReady;
    }

private:
    LoginBootstrapConfig config_;
    LoginPhase phase_ = LoginPhase::Idle;
    NetworkSettingsInfo settings_{};
    bool compression_enabled_ = false;
    std::string last_error_;

    void fail(std::string* error, const char* msg);
    static std::vector<std::uint8_t> make_request_network_settings_body();
    bool parse_network_settings(const std::vector<std::uint8_t>& payload,
                                NetworkSettingsInfo& out,
                                std::string* error);
};

} // namespace bedrockai
