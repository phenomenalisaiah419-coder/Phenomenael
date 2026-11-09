#include "core/game_batch.hpp"
#include "core/network_engine.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace bedrockai;

class BatchBackend final : public BedrockTransportBackend {
public:
    bool connect(const ProtocolIdentity& identity, std::string* error) override {
        if (!identity.is_target()) { if (error) *error = "wrong protocol"; return false; }
        state_ = TransportState::Authenticating;
        return true;
    }
    bool send(const TransportFrame& frame, std::string*) override { sent.push_back(frame); return true; }
    void close() override { state_ = TransportState::Disconnected; }
    TransportState state() const override { return state_; }
    void set_receive_handler(ReceiveHandler handler) override { handler_ = std::move(handler); }
    std::vector<TransportFrame> sent;
private:
    TransportState state_ = TransportState::Disconnected;
    ReceiveHandler handler_;
};

static void test_batch_roundtrip_uncompressed() {
    wire::BatchCodecConfig c;
    c.compression_enabled = false;
    const std::vector<std::vector<std::uint8_t>> packets = {{0x90, 0x01, 0x02}, {0x1e, 0xaa}};
    auto enc = wire::BedrockBatchCodec::encode(packets, c);
    assert(enc);
    assert((*enc)[0] == 0xfe);
    auto dec = wire::BedrockBatchCodec::decode(*enc, c);
    assert(dec && dec->size() == 2);
    assert((*dec)[0].packet_id == 0x90);
    assert((*dec)[0].payload == std::vector<std::uint8_t>({0x02}));
    assert((*dec)[1].packet_id == 0x1e);
}

static void test_batch_roundtrip_flate() {
    wire::BatchCodecConfig c;
    c.compression_enabled = true;
    c.compression = wire::CompressionAlgorithm::Flate;
    c.compression_threshold = 1;
    std::vector<std::uint8_t> payload(4000, 0x41);
    std::vector<std::uint8_t> second{30};
    second.insert(second.end(), payload.begin(), payload.end());
    const std::vector<std::vector<std::uint8_t>> packets = {{144, 1, 2, 3}, second};
    auto enc = wire::BedrockBatchCodec::encode(packets, c);
    assert(enc);
    assert((*enc)[0] == 0xfe);
    assert((*enc)[1] == 0);
    auto dec = wire::BedrockBatchCodec::decode(*enc, c);
    assert(dec && dec->size() == 2);
    assert((*dec)[1].payload.size() == 4000);
}


static void test_batch_rejects_corruption_and_limits() {
    wire::BatchCodecConfig c;
    auto bad = std::vector<std::uint8_t>{0xfe, 0x00, 0x01};
    wire::BatchError e;
    auto dec = wire::BedrockBatchCodec::decode(bad, c, &e);
    assert(!dec);

    c.compression_enabled = true;
    c.compression = wire::CompressionAlgorithm::Flate;
    c.max_uncompressed_bytes = 32;
    std::vector<std::uint8_t> p(100, 0x41);
    auto oversized = wire::BedrockBatchCodec::encode({std::vector<std::uint8_t>{30}, p}, c, &e);
    assert(!oversized);
}

static void test_engine_queues_batch() {
    NetworkEngineConfig c;
    c.batch.compression_enabled = false;
    Protocol2193NetworkEngine engine(c);
    BatchBackend backend;
    std::string error;
    assert(engine.attach(backend, &error));
    assert(engine.queue_game_packet(packet_id::PlayerAuthInput, {1,2,3}, &error));
    assert(engine.flush_batch(&error));
    assert(backend.sent.size() == 1);
    assert(backend.sent[0].packet_id == 0xfe);
    assert(backend.sent[0].payload.front() == 0xfe);
}

int main() {
    test_batch_roundtrip_uncompressed();
    test_batch_roundtrip_flate();
    test_batch_rejects_corruption_and_limits();
    test_engine_queues_batch();
    std::cout << "v0.8 network batch tests: PASS\n";
}
