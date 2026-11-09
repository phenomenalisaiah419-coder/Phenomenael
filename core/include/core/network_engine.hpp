#pragma once

#include "bedrock_crypto.hpp"
#include "game_batch.hpp"
#include "transport.hpp"

#include <cstddef>
#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bedrockai {

struct NetworkEngineConfig {
    ProtocolIdentity expected{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::Unknown};
    wire::BatchCodecConfig batch{};
    std::size_t max_serverbound_packets_per_batch = 32;
};

class Protocol2193NetworkEngine {
public:
    using ReceiveHandler = std::function<void(const TransportFrame&)>;
    explicit Protocol2193NetworkEngine(NetworkEngineConfig config = {});

    bool attach(BedrockTransportBackend& backend, std::string* error = nullptr);
    bool queue_game_packet(std::uint32_t packet_id, std::vector<std::uint8_t> payload, std::string* error = nullptr);
    bool flush_batch(std::string* error = nullptr);
    bool ingest_batch(const std::vector<std::uint8_t>& bytes, std::string* error = nullptr);
    bool ingest_wire_batch(std::vector<std::uint8_t> bytes, std::string* error = nullptr);
    void set_packet_cipher(BedrockPacketCipher* cipher) { cipher_ = cipher; }
    bool encryption_enabled() const { return cipher_ != nullptr && cipher_->enabled(); }
    const std::string& last_error() const { return last_error_; }
    bool healthy() const { return last_error_.empty(); }
    void set_receive_handler(ReceiveHandler handler) { receive_handler_ = std::move(handler); }
    void set_compression(bool enabled, wire::CompressionAlgorithm algorithm, std::size_t threshold);
    void detach();

    std::size_t pending_game_packets() const { return pending_.size(); }
    TransportState state() const { return session_.state(); }
    Protocol2193Session& session() { return session_; }
    const Protocol2193Session& session() const { return session_; }

private:
    NetworkEngineConfig config_;
    Protocol2193Session session_;
    BedrockTransportBackend* backend_ = nullptr;
    std::vector<std::vector<std::uint8_t>> pending_;
    BedrockPacketCipher* cipher_ = nullptr;
    ReceiveHandler receive_handler_{};
    std::string last_error_;

    void on_session_frame(const TransportFrame& frame);
    bool send_batch_bytes(const std::vector<std::uint8_t>& batch, std::string* error);
};

} // namespace bedrockai
