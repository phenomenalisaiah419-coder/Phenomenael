#include "core/network_engine.hpp"

namespace bedrockai {
namespace {
void set_err(std::string* e, const char* s) { if (e) *e = s; }
void put_varu32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    while (v >= 0x80u) { out.push_back(static_cast<std::uint8_t>(v) | 0x80u); v >>= 7; }
    out.push_back(static_cast<std::uint8_t>(v));
}
}

Protocol2193NetworkEngine::Protocol2193NetworkEngine(NetworkEngineConfig config)
    : config_(std::move(config)), session_(TransportSessionConfig{config_.expected, 128}) {}

bool Protocol2193NetworkEngine::attach(BedrockTransportBackend& backend, std::string* error) {
    if (backend_) { set_err(error, "network engine already attached"); return false; }
    if (!session_.attach(backend, error)) return false;
    backend_ = &backend;
    session_.set_receive_handler([this](const TransportFrame& frame) { on_session_frame(frame); });
    return true;
}

bool Protocol2193NetworkEngine::queue_game_packet(std::uint32_t packet_id, std::vector<std::uint8_t> payload, std::string* error) {
    if (!backend_) { set_err(error, "no network backend attached"); return false; }
    if (pending_.size() >= config_.max_serverbound_packets_per_batch) { set_err(error, "serverbound batch packet limit reached"); return false; }
    if (packet_id > 0xffffu) { set_err(error, "game packet id out of supported range"); return false; }
    std::vector<std::uint8_t> packet;
    put_varu32(packet, packet_id);
    packet.insert(packet.end(), payload.begin(), payload.end());
    pending_.push_back(std::move(packet));
    return true;
}

bool Protocol2193NetworkEngine::flush_batch(std::string* error) {
    if (!backend_) { set_err(error, "no network backend attached"); return false; }
    if (pending_.empty()) return true;
    wire::BatchError be;
    auto batch = wire::BedrockBatchCodec::encode(pending_, config_.batch, &be);
    if (!batch) { if (error) *error = be.message; return false; }
    if (!send_batch_bytes(*batch, error)) return false;
    pending_.clear();
    return true;
}

bool Protocol2193NetworkEngine::ingest_batch(const std::vector<std::uint8_t>& bytes, std::string* error) {
    if (!backend_) { set_err(error, "no network backend attached"); return false; }
    wire::BatchError be;
    auto packets = wire::BedrockBatchCodec::decode(bytes, config_.batch, &be);
    if (!packets) { if (error) *error = be.message; return false; }
    for (const auto& p : *packets) {
        TransportFrame frame;
        frame.identity = config_.expected;
        frame.direction = Direction::Clientbound;
        frame.packet_id = p.packet_id;
        frame.sequence = 0;
        frame.payload = p.payload;
        if (!session_.ingest(frame, error)) return false;
        if (receive_handler_) receive_handler_(frame);
    }
    return true;
}

bool Protocol2193NetworkEngine::ingest_wire_batch(std::vector<std::uint8_t> bytes, std::string* error) {
    if (bytes.empty()) { set_err(error, "wire batch is empty"); return false; }
    if (cipher_ && cipher_->enabled()) {
        if (!cipher_->decrypt_packet(bytes, error)) return false;
    }
    return ingest_batch(bytes, error);
}

void Protocol2193NetworkEngine::on_session_frame(const TransportFrame& frame) {
    std::string error;
    if (frame.direction == Direction::Clientbound && frame.packet_id == 0xfe) {
        if (!ingest_wire_batch(frame.payload, &error)) {
            // A registered consumer receives no malformed packet. The transport itself
            // remains responsible for surfacing the fatal error; this seam only fans out
            // validated game packets.
        }
        return;
    }
    if (receive_handler_) receive_handler_(frame);
}

void Protocol2193NetworkEngine::set_compression(bool enabled, wire::CompressionAlgorithm algorithm, std::size_t threshold) {
    config_.batch.compression_enabled = enabled;
    config_.batch.compression = algorithm;
    config_.batch.compression_threshold = threshold;
}

void Protocol2193NetworkEngine::detach() {
    pending_.clear();
    last_error_.clear();
    cipher_ = nullptr;
    session_.detach();
    backend_ = nullptr;
}

bool Protocol2193NetworkEngine::send_batch_bytes(const std::vector<std::uint8_t>& batch, std::string* error) {
    // The Bedrock packet cipher wraps the complete 0xFE batch after compression and before
    // RakNet encapsulation. Keep the original batch untouched for callers.
    std::vector<std::uint8_t> wire = batch;
    if (cipher_ && cipher_->enabled()) {
        if (!cipher_->encrypt_packet(wire, error)) return false;
    }
    TransportFrame frame;
    frame.identity = config_.expected;
    frame.direction = Direction::Serverbound;
    frame.packet_id = 0xfe;
    frame.sequence = 1;
    frame.payload = std::move(wire);
    return backend_->send(frame, error);
}

} // namespace bedrockai
