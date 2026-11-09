#pragma once

#include "protocol_adapter.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bedrockai::wire {

enum class CompressionAlgorithm : std::uint8_t {
    Flate = 0,
    Snappy = 1,
    None = 0xff
};

struct BatchCodecConfig {
    bool compression_enabled = false;
    CompressionAlgorithm compression = CompressionAlgorithm::Flate;
    std::size_t compression_threshold = 0;
    std::size_t max_uncompressed_bytes = 16 * 1024 * 1024;
    std::size_t max_packets = 812;
};

struct BatchError {
    std::string message;
    std::size_t offset = 0;
};

struct DecodedGamePacket {
    std::uint32_t packet_id = 0;
    std::vector<std::uint8_t> payload; // excludes packet-id varuint32
};

class BedrockBatchCodec {
public:
    static constexpr std::uint8_t kBatchHeader = 0xfe;

    static std::optional<std::vector<std::uint8_t>> encode(
        const std::vector<std::vector<std::uint8_t>>& game_packets,
        const BatchCodecConfig& config,
        BatchError* error = nullptr);

    static std::optional<std::vector<DecodedGamePacket>> decode(
        const std::vector<std::uint8_t>& bytes,
        const BatchCodecConfig& config,
        BatchError* error = nullptr);

    static std::optional<std::vector<std::uint8_t>> decode_game_packet(
        const std::vector<std::uint8_t>& packet,
        BatchError* error = nullptr);
};

} // namespace bedrockai::wire
