#include "core/secure_runtime.hpp"

#include <utility>

namespace bedrockai {
namespace {

bool same_identity(const ProtocolIdentity& a, const ProtocolIdentity& b) {
    return a.game_version == b.game_version && a.protocol == b.protocol;
}

} // namespace

Bedrock2193SecureRuntime::Bedrock2193SecureRuntime(BedrockCryptoProvider& crypto,
                                                   SecureRuntimeConfig config)
    : crypto_(crypto),
      config_(std::move(config)),
      network_(config_.network),
      bootstrap_(config_.bootstrap),
      login_builder_(crypto_),
      handshake_processor_(crypto_) {}

bool Bedrock2193SecureRuntime::fail(std::string* error, const std::string& message) {
    last_error_ = message;
    if (error) *error = last_error_;
    return false;
}

bool Bedrock2193SecureRuntime::attach(BedrockTransportBackend& backend, std::string* error) {
    if (backend_) return fail(error, "secure runtime is already attached");
    if (!config_.network.expected.is_target() || !config_.bootstrap.expected.is_target()) {
        return fail(error, "secure runtime requires Bedrock 1.26.50 / 2193");
    }
    if (!same_identity(config_.network.expected, config_.bootstrap.expected)) {
        return fail(error, "network and bootstrap protocol identities do not match");
    }
    if (config_.require_encryption != config_.bootstrap.require_encryption) {
        return fail(error, "secure runtime encryption policy does not match bootstrap policy");
    }
    if (!network_.attach(backend, error)) return false;
    backend_ = &backend;
    network_.set_receive_handler([this](const TransportFrame& frame) {
        std::string local_error;
        if (!handle_frame(frame, &local_error)) {
            last_error_ = local_error.empty() ? "secure runtime frame handling failed" : local_error;
        }
    });
    last_error_.clear();
    return true;
}

bool Bedrock2193SecureRuntime::start_login(BedrockLoginAuthProvider& auth,
                                           LoginAuthRequestContext context,
                                           std::string* error) {
    if (!backend_) return fail(error, "secure runtime has no transport");
    if (auth_) return fail(error, "secure runtime login has already started");
    if (!context.protocol.is_target()) return fail(error, "login context is not pinned to Bedrock 1.26.50 / 2193");
    if (context.server_address.empty()) return fail(error, "login context server address is empty");
    auth_ = &auth;
    auth_context_ = std::move(context);
    if (!pump(error)) {
        auth_ = nullptr;
        return false;
    }
    last_error_.clear();
    return true;
}

bool Bedrock2193SecureRuntime::pump(std::string* error) {
    if (!backend_) return fail(error, "secure runtime has no transport");
    if (!network_.healthy()) return fail(error, network_.last_error());
    if (auth_ && bootstrap_.phase() == LoginPhase::Idle) {
        const auto st = network_.state();
        if (st == TransportState::Authenticating || st == TransportState::InGame) {
            if (!bootstrap_.start(network_, error)) return false;
        } else if (st == TransportState::Failed || st == TransportState::Closing || st == TransportState::Disconnected) {
            return fail(error, "transport is not available for login bootstrap");
        }
    }
    return true;
}

bool Bedrock2193SecureRuntime::handle_network_settings(const TransportFrame& frame, std::string* error) {
    if (!bootstrap_.handle_clientbound(frame, error)) return false;
    if (!auth_) return fail(error, "NetworkSettings received without an auth provider");
    const auto& settings = bootstrap_.network_settings();
    if (settings.compression_algorithm == 1) {
        return fail(error, "Snappy compression is advertised but not implemented in this build");
    }
    wire::CompressionAlgorithm algorithm = wire::CompressionAlgorithm::None;
    if (settings.compression_algorithm == 0) algorithm = wire::CompressionAlgorithm::Flate;
    network_.set_compression(bootstrap_.compression_enabled(), algorithm, settings.compression_threshold);

    if (auto* prebuilt = dynamic_cast<BedrockPrebuiltLoginProvider*>(auth_)) {
        if (!prebuilt->get_prebuilt_login(auth_context_, artifact_, error)) return false;
        if (artifact_.login_packet_payload.empty() || artifact_.identity_key.private_key_pkcs8_der.empty()) {
            return fail(error, "prebuilt login provider returned an incomplete Login artifact");
        }
    } else {
        auth_context_.client_public_key_base64.clear();
        if (!login_builder_.build(auth_context_, *auth_, artifact_, error)) return false;
    }
    if (!bootstrap_.provide_login_payload(network_, artifact_.login_packet_payload, error)) return false;
    return true;
}

bool Bedrock2193SecureRuntime::handle_server_handshake(const TransportFrame& frame, std::string* error) {
    if (!config_.require_encryption) return fail(error, "server encryption handshake received while encryption is disabled");
    if (bootstrap_.phase() != LoginPhase::EncryptionNegotiating) {
        return fail(error, "ServerToClientHandshake received outside encryption negotiation phase");
    }
    if (artifact_.identity_key.private_key_pkcs8_der.empty()) {
        return fail(error, "client identity key is not available for server handshake");
    }
    if (!handshake_processor_.process(frame.payload, artifact_.identity_key.private_key_pkcs8_der,
                                      server_handshake_, error)) return false;
    if (!cipher_.enable(server_handshake_.encryption_key, error)) return false;
    network_.set_packet_cipher(&cipher_);

    // ClientToServerHandshake has an empty protocol payload, but it is still a
    // normal encrypted Bedrock batch after the server has enabled encryption.
    if (!network_.queue_game_packet(packet_id::ClientToServerHandshake, {}, error)) return false;
    if (!network_.flush_batch(error)) return false;
    if (!bootstrap_.mark_encryption_established(error)) return false;
    return true;
}

bool Bedrock2193SecureRuntime::handle_start_game(const TransportFrame& frame, std::string* error) {
    (void)frame;
    if (bootstrap_.phase() != LoginPhase::Authenticated) {
        return fail(error, "StartGame received before authenticated phase");
    }
    return bootstrap_.mark_world_ready(error);
}

bool Bedrock2193SecureRuntime::handle_frame(const TransportFrame& frame, std::string* error) {
    if (!frame.identity.is_target()) return fail(error, "secure runtime received a non-2193 frame");
    bool handled = false;
    if (frame.direction == Direction::Clientbound && frame.packet_id == packet_id::NetworkSettings) {
        handled = true;
        if (!handle_network_settings(frame, error)) return false;
    } else if (frame.direction == Direction::Clientbound && frame.packet_id == packet_id::ServerToClientHandshake) {
        handled = true;
        if (!handle_server_handshake(frame, error)) return false;
    } else if (frame.direction == Direction::Clientbound && frame.packet_id == packet_id::StartGame) {
        handled = true;
        if (!handle_start_game(frame, error)) return false;
        // StartGame is a lifecycle packet for login bootstrap and the first authoritative
        // world observation for gameplay intelligence. Handle it here, then forward it.
        forward(frame);
        last_error_.clear();
        return true;
    } else if (frame.direction == Direction::Clientbound && frame.packet_id == packet_id::Disconnect) {
        bootstrap_.close();
        handled = true;
    }

    if (!handled) forward(frame);
    last_error_.clear();
    return true;
}

void Bedrock2193SecureRuntime::forward(const TransportFrame& frame) {
    if (receive_handler_) receive_handler_(frame);
}

void Bedrock2193SecureRuntime::detach() {
    network_.detach();
    backend_ = nullptr;
    auth_ = nullptr;
    cipher_.disable();
    artifact_ = LoginArtifact{};
    server_handshake_ = ServerEncryptionHandshake{};
    last_error_.clear();
}

} // namespace bedrockai
