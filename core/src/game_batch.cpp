#include "core/game_batch.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <zlib.h>

namespace bedrockai::wire {
namespace {

void set_error(BatchError* e, const char* msg, std::size_t off) {
    if (e) { e->message = msg; e->offset = off; }
}

void put_varu32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    while (v >= 0x80u) { out.push_back(static_cast<std::uint8_t>(v) | 0x80u); v >>= 7; }
    out.push_back(static_cast<std::uint8_t>(v));
}

bool get_varu32(const std::vector<std::uint8_t>& b, std::size_t& p, std::uint32_t& out) {
    out = 0;
    for (unsigned i = 0; i < 5; ++i) {
        if (p >= b.size()) return false;
        const auto x = b[p++];
        if (i == 4 && x > 1) return false;
        out |= static_cast<std::uint32_t>(x & 0x7fu) << (7 * i);
        if ((x & 0x80u) == 0) return true;
    }
    return false;
}

bool raw_deflate(const std::vector<std::uint8_t>& src, std::vector<std::uint8_t>& dst) {
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    dst.resize(compressBound(static_cast<uLong>(src.size())));
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src.data()));
    zs.avail_in = static_cast<uInt>(src.size());
    zs.next_out = reinterpret_cast<Bytef*>(dst.data());
    zs.avail_out = static_cast<uInt>(dst.size());
    const int rc = deflate(&zs, Z_FINISH);
    const bool ok = rc == Z_STREAM_END;
    if (ok) dst.resize(zs.total_out); else dst.clear();
    deflateEnd(&zs);
    return ok;
}

bool raw_inflate(const std::vector<std::uint8_t>& src, std::size_t max_size, std::vector<std::uint8_t>& dst) {
    z_stream zs{};
    if (inflateInit2(&zs, -15) != Z_OK) return false;
    dst.clear();
    dst.reserve(std::min<std::size_t>(max_size, std::max<std::size_t>(256, src.size() * 2)));
    std::array<std::uint8_t, 64 * 1024> buf{};
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src.data()));
    zs.avail_in = static_cast<uInt>(src.size());
    int rc = Z_OK;
    while (rc == Z_OK) {
        zs.next_out = reinterpret_cast<Bytef*>(buf.data());
        zs.avail_out = static_cast<uInt>(buf.size());
        rc = inflate(&zs, Z_NO_FLUSH);
        const std::size_t produced = buf.size() - zs.avail_out;
        if (dst.size() + produced > max_size) { inflateEnd(&zs); dst.clear(); return false; }
        dst.insert(dst.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(produced));
    }
    inflateEnd(&zs);
    return rc == Z_STREAM_END;
}

std::optional<std::vector<std::uint8_t>> decode_game_packet_impl(const std::vector<std::uint8_t>& p, BatchError* e) {
    std::size_t pos = 0;
    std::uint32_t id = 0;
    if (!get_varu32(p, pos, id)) {
        set_error(e, "invalid game packet varuint32/id", pos);
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(p.begin() + static_cast<std::ptrdiff_t>(pos), p.end());
}

} // namespace

std::optional<std::vector<std::uint8_t>> BedrockBatchCodec::encode(
    const std::vector<std::vector<std::uint8_t>>& game_packets,
    const BatchCodecConfig& config, BatchError* e) {
    if (game_packets.empty()) { set_error(e, "cannot encode an empty batch", 0); return std::nullopt; }
    if (game_packets.size() > config.max_packets) { set_error(e, "batch packet count exceeds limit", 0); return std::nullopt; }

    std::vector<std::uint8_t> raw;
    for (const auto& p : game_packets) {
        if (p.empty()) { set_error(e, "empty game packet", raw.size()); return std::nullopt; }
        if (p.size() > std::numeric_limits<std::uint32_t>::max()) { set_error(e, "game packet too large", raw.size()); return std::nullopt; }
        put_varu32(raw, static_cast<std::uint32_t>(p.size()));
        raw.insert(raw.end(), p.begin(), p.end());
        if (raw.size() > config.max_uncompressed_bytes) { set_error(e, "batch exceeds uncompressed size limit", raw.size()); return std::nullopt; }
    }

    std::vector<std::uint8_t> body;
    std::uint8_t marker = static_cast<std::uint8_t>(CompressionAlgorithm::None);
    if (config.compression_enabled && config.compression == CompressionAlgorithm::Flate && raw.size() >= config.compression_threshold) {
        if (!raw_deflate(raw, body)) { set_error(e, "raw deflate compression failed", 1); return std::nullopt; }
        marker = static_cast<std::uint8_t>(CompressionAlgorithm::Flate);
    } else {
        body = raw;
    }

    std::vector<std::uint8_t> out;
    out.reserve(2 + body.size());
    out.push_back(kBatchHeader);
    if (config.compression_enabled) out.push_back(marker);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::optional<std::vector<DecodedGamePacket>> BedrockBatchCodec::decode(
    const std::vector<std::uint8_t>& bytes,
    const BatchCodecConfig& config, BatchError* e) {
    if (bytes.empty() || bytes[0] != kBatchHeader) { set_error(e, "invalid batch header", 0); return std::nullopt; }
    std::size_t pos = 1;
    std::vector<std::uint8_t> raw;
    if (config.compression_enabled) {
        if (pos >= bytes.size()) { set_error(e, "missing compression marker", pos); return std::nullopt; }
        const auto marker = bytes[pos++];
        std::vector<std::uint8_t> body(bytes.begin() + static_cast<std::ptrdiff_t>(pos), bytes.end());
        if (marker == static_cast<std::uint8_t>(CompressionAlgorithm::None)) {
            raw = std::move(body);
        } else if (marker == static_cast<std::uint8_t>(CompressionAlgorithm::Flate)) {
            if (config.compression != CompressionAlgorithm::Flate) { set_error(e, "unexpected compression algorithm", pos - 1); return std::nullopt; }
            if (!raw_inflate(body, config.max_uncompressed_bytes, raw)) { set_error(e, "raw deflate decompression failed or exceeded limit", pos); return std::nullopt; }
        } else {
            set_error(e, "unsupported compression algorithm", pos - 1);
            return std::nullopt;
        }
    } else {
        raw.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos), bytes.end());
    }
    if (raw.empty()) { set_error(e, "empty batch payload", pos); return std::nullopt; }

    std::vector<DecodedGamePacket> result;
    std::size_t p = 0;
    while (p < raw.size()) {
        if (result.size() >= config.max_packets) { set_error(e, "batch packet count exceeds limit", p); return std::nullopt; }
        std::uint32_t len = 0;
        if (!get_varu32(raw, p, len) || len == 0 || p + static_cast<std::size_t>(len) > raw.size()) {
            set_error(e, "invalid inner packet length", p);
            return std::nullopt;
        }
        const std::size_t end = p + static_cast<std::size_t>(len);
        std::vector<std::uint8_t> packet(raw.begin() + static_cast<std::ptrdiff_t>(p), raw.begin() + static_cast<std::ptrdiff_t>(end));
        p = end;
        std::size_t gp = 0;
        std::uint32_t id = 0;
        if (!get_varu32(packet, gp, id) || gp > packet.size()) { set_error(e, "invalid inner game packet", p); return std::nullopt; }
        result.push_back(DecodedGamePacket{id, std::vector<std::uint8_t>(packet.begin() + static_cast<std::ptrdiff_t>(gp), packet.end())});
    }
    return result;
}

std::optional<std::vector<std::uint8_t>> BedrockBatchCodec::decode_game_packet(
    const std::vector<std::uint8_t>& packet, BatchError* e) {
    return decode_game_packet_impl(packet, e);
}

} // namespace bedrockai::wire
