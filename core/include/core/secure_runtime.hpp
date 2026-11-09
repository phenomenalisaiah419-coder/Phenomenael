#pragma once

#include "bedrock_handshake.hpp"
#include "login_payload.hpp"
#include "network_engine.hpp"
#include "session_bootstrap.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace bedrockai {

struct SecureRuntimeConfig {
    NetworkEngineConfig network{};
    LoginBootstrapConfig bootstrap{};
    bool require_encryption = true;
};

class Bedrock2193SecureRuntime {
public:
    using ReceiveHandler = std::function<void(const TransportFrame&)>;

    explicit Bedrock2193SecureRuntime(BedrockCryptoProvider& crypto,
                                      SecureRuntimeConfig config = {});

    // Attaches and starts the underlying transport. For RakNet, the host still
    // drives DatagramIo/RakNetTransportBackend::poll().
    bool attach(BedrockTransportBackend& backend, std::string* error = nullptr);

    // Starts RequestNetworkSettings. Login credentials are requested from the
    // supplied host-owned provider only after NetworkSettings arrives.
    bool start_login(BedrockLoginAuthProvider& auth,
                     LoginAuthRequestContext context,
                     std::string* error = nullptr);

    // Advances the transport-to-bootstrap boundary. For RakNet this should be
    // called after the host drives RakNetTransportBackend::poll(). It starts
    // RequestNetworkSettings once the connected transport is actually ready.
    bool pump(std::string* error = nullptr);

    void set_receive_handler(ReceiveHandler handler) { receive_handler_ = std::move(handler); }

    // Handles the transport/session callback seam. Normally invoked internally
    // through Protocol2193NetworkEngine.
    bool handle_frame(const TransportFrame& frame, std::string* error = nullptr);

    void detach();

    LoginPhase phase() const { return bootstrap_.phase(); }
    bool encryption_enabled() const { return cipher_.enabled(); }
    const LoginArtifact& login_artifact() const { return artifact_; }
    const ServerEncryptionHandshake& server_handshake() const { return server_handshake_; }
    const Protocol2193NetworkEngine& network() const { return network_; }
    Protocol2193NetworkEngine& network() { return network_; }
    BedrockPacketCipher& cipher() { return cipher_; }
    const std::string& last_error() const { return last_error_; }

private:
    BedrockCryptoProvider& crypto_;
    SecureRuntimeConfig config_;
    Protocol2193NetworkEngine network_;
    Bedrock2193LoginBootstrap bootstrap_;
    Bedrock2193LoginPayloadBuilder login_builder_;
    BedrockServerToClientHandshakeProcessor handshake_processor_;
    BedrockPacketCipher cipher_;
    BedrockTransportBackend* backend_ = nullptr;
    BedrockLoginAuthProvider* auth_ = nullptr;
    LoginAuthRequestContext auth_context_{};
    LoginArtifact artifact_{};
    ServerEncryptionHandshake server_handshake_{};
    ReceiveHandler receive_handler_{};
    std::string last_error_;

    bool fail(std::string* error, const std::string& message);
    void forward(const TransportFrame& frame);
    bool handle_network_settings(const TransportFrame& frame, std::string* error);
    bool handle_server_handshake(const TransportFrame& frame, std::string* error);
    bool handle_start_game(const TransportFrame& frame, std::string* error);
};

} // namespace bedrockai
