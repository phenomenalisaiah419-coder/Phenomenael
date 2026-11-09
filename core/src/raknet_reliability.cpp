#include "core/raknet_reliability.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace bedrockai {
namespace raknet {
namespace {

constexpr std::size_t kFrameSetHeaderBytes = 4; // ID + uint24le datagram sequence.
constexpr std::size_t kMaxFrameBodyBytes = 8191; // uint16 bit-length stores body bytes*8.
constexpr std::size_t kSplitMetaBytes = 10; // count u32 + id u16 + index u32.
constexpr std::size_t kMaxAckNackRecords = 256;

void put_be16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
}

void put_be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
}

bool take_be16(const std::vector<std::uint8_t>& b, std::size_t& p, std::uint16_t& v) {
    if (p + 2 > b.size()) return false;
    v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[p]) << 8) | b[p + 1]);
    p += 2;
    return true;
}

bool take_be32(const std::vector<std::uint8_t>& b, std::size_t& p, std::uint32_t& v) {
    if (p + 4 > b.size()) return false;
    v = (static_cast<std::uint32_t>(b[p]) << 24) |
        (static_cast<std::uint32_t>(b[p + 1]) << 16) |
        (static_cast<std::uint32_t>(b[p + 2]) << 8) |
        static_cast<std::uint32_t>(b[p + 3]);
    p += 4;
    return true;
}

void add_linear_range(std::vector<std::pair<std::uint32_t, std::uint32_t>>& ranges,
                      std::uint32_t value,
                      std::size_t max_ranges) {
    value &= kRakNetSequenceMask;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        auto& r = ranges[i];
        if (value >= r.first && value <= r.second) return;
        if (r.second < kRakNetSequenceMask && value == r.second + 1u) {
            r.second = value;
            if (i + 1 < ranges.size() && ranges[i + 1].first == r.second + 1u) {
                r.second = ranges[i + 1].second;
                ranges.erase(ranges.begin() + static_cast<std::ptrdiff_t>(i + 1));
            }
            return;
        }
        if (r.first > 0 && value + 1u == r.first) {
            r.first = value;
            return;
        }
        if (value < r.first) {
            if (ranges.size() < max_ranges) ranges.insert(ranges.begin() + static_cast<std::ptrdiff_t>(i), {value, value});
            return;
        }
    }
    if (ranges.size() < max_ranges) ranges.emplace_back(value, value);
}

void remove_linear_value(std::vector<std::pair<std::uint32_t, std::uint32_t>>& ranges,
                         std::uint32_t value) {
    value &= kRakNetSequenceMask;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        auto& r = ranges[i];
        if (value < r.first || value > r.second) continue;
        if (r.first == r.second) {
            ranges.erase(ranges.begin() + static_cast<std::ptrdiff_t>(i));
        } else if (value == r.first) {
            ++r.first;
        } else if (value == r.second) {
            --r.second;
        } else {
            const auto old_end = r.second;
            r.second = value - 1u;
            ranges.emplace_back(value + 1u, old_end);
        }
        return;
    }
}

} // namespace

RakNetReliabilityEngine::RakNetReliabilityEngine(ReliabilityConfig config)
    : config_(std::move(config)),
      next_order_indices_(config_.max_order_channels, 0),
      next_sequence_indices_(config_.max_order_channels, 0),
      order_states_(config_.max_order_channels) {
    if (config_.mtu < kFrameSetHeaderBytes + 16) config_.mtu = static_cast<std::uint16_t>(kFrameSetHeaderBytes + 16);
    if (config_.max_datagram_bytes < config_.mtu) config_.mtu = static_cast<std::uint16_t>(config_.max_datagram_bytes);
}

bool RakNetReliabilityEngine::is_reliable(RakNetReliability reliability) {
    switch (reliability) {
        case RakNetReliability::Reliable:
        case RakNetReliability::ReliableOrdered:
        case RakNetReliability::ReliableSequenced:
        case RakNetReliability::ReliableWithAckReceipt:
        case RakNetReliability::ReliableOrderedWithAckReceipt:
            return true;
        default:
            return false;
    }
}

bool RakNetReliabilityEngine::is_sequenced(RakNetReliability reliability) {
    return reliability == RakNetReliability::UnreliableSequenced ||
           reliability == RakNetReliability::ReliableSequenced;
}

bool RakNetReliabilityEngine::is_ordered(RakNetReliability reliability) {
    return reliability == RakNetReliability::UnreliableSequenced ||
           reliability == RakNetReliability::ReliableOrdered ||
           reliability == RakNetReliability::ReliableSequenced ||
           reliability == RakNetReliability::ReliableOrderedWithAckReceipt;
}

std::size_t RakNetReliabilityEngine::frame_header_size(RakNetReliability reliability, bool split) {
    std::size_t n = 1 + 2;
    if (is_reliable(reliability)) n += 3;
    if (is_sequenced(reliability)) n += 3;
    if (is_ordered(reliability)) n += 3 + 1;
    if (split) n += kSplitMetaBytes;
    return n;
}

void RakNetReliabilityEngine::fail(const char* message) {
    terminal_ = true;
    last_error_ = message ? message : "RakNet reliability error";
}

void RakNetReliabilityEngine::write_u24_le(std::vector<std::uint8_t>& b, std::uint32_t value) {
    value &= kRakNetSequenceMask;
    b.push_back(static_cast<std::uint8_t>(value & 0xff));
    b.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    b.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
}

std::uint32_t RakNetReliabilityEngine::read_u24_le(const std::vector<std::uint8_t>& b,
                                                   std::size_t& p,
                                                   bool* ok) {
    if (p + 3 > b.size()) {
        if (ok) *ok = false;
        return 0;
    }
    const auto v = static_cast<std::uint32_t>(b[p]) |
                   (static_cast<std::uint32_t>(b[p + 1]) << 8) |
                   (static_cast<std::uint32_t>(b[p + 2]) << 16);
    p += 3;
    if (ok) *ok = true;
    return v;
}

std::uint32_t RakNetReliabilityEngine::next_order_index(std::uint8_t channel) const {
    if (channel >= next_order_indices_.size()) return 0;
    return next_order_indices_[channel] & kRakNetSequenceMask;
}

bool RakNetReliabilityEngine::seq_newer(std::uint32_t a, std::uint32_t b) {
    const auto diff = (a - b) & kRakNetSequenceMask;
    return diff != 0 && diff < 0x00800000u;
}

bool RakNetReliabilityEngine::queue_datagram(std::vector<std::uint8_t> bytes,
                                             std::uint32_t sequence,
                                             bool reliable,
                                             std::string* error) {
    if (bytes.empty()) {
        if (error) *error = "cannot queue empty RakNet datagram";
        return false;
    }
    if (bytes.size() > config_.max_datagram_bytes || bytes.size() > config_.mtu) {
        if (error) *error = "RakNet datagram exceeds negotiated MTU";
        return false;
    }
    if (outbound_.size() >= config_.max_pending_datagrams) {
        if (error) *error = "RakNet outbound datagram queue full";
        return false;
    }
    outbound_.push_back(PendingDatagram{std::move(bytes), sequence & kRakNetSequenceMask, reliable});
    return true;
}

bool RakNetReliabilityEngine::build_frame_datagram(const std::vector<std::uint8_t>& frame,
                                                   std::uint32_t sequence,
                                                   std::vector<std::uint8_t>& datagram,
                                                   std::string* error) const {
    datagram.clear();
    datagram.reserve(kFrameSetHeaderBytes + frame.size());
    datagram.push_back(kFrameSetMin);
    write_u24_le(datagram, sequence);
    datagram.insert(datagram.end(), frame.begin(), frame.end());
    if (datagram.size() > config_.mtu || datagram.size() > config_.max_datagram_bytes) {
        if (error) *error = "RakNet datagram exceeds MTU";
        return false;
    }
    return true;
}

bool RakNetReliabilityEngine::encode_message_frame(const std::vector<std::uint8_t>& payload,
                                                   RakNetReliability reliability,
                                                   std::uint8_t order_channel,
                                                   std::uint32_t reliable_index,
                                                   std::uint32_t sequence_index,
                                                   std::uint32_t order_index,
                                                   bool split,
                                                   std::uint32_t split_count,
                                                   std::uint16_t split_id,
                                                   std::uint32_t split_index,
                                                   std::vector<std::uint8_t>& out,
                                                   std::string* error) const {
    if (is_ordered(reliability) && order_channel >= config_.max_order_channels) {
        if (error) *error = "RakNet order channel exceeds configured limit";
        return false;
    }
    if (payload.size() > kMaxFrameBodyBytes) {
        if (error) *error = "RakNet frame body exceeds uint16 bit-length capacity";
        return false;
    }
    if (split && (split_count == 0 || split_count > config_.max_split_count || split_index >= split_count)) {
        if (error) *error = "invalid RakNet split metadata";
        return false;
    }
    out.clear();
    const auto reliability_bits = static_cast<std::uint8_t>(reliability);
    out.push_back(static_cast<std::uint8_t>((reliability_bits << 5) | (split ? 0x10 : 0x00)));
    put_be16(out, static_cast<std::uint16_t>(payload.size() * 8u));
    if (is_reliable(reliability)) write_u24_le(out, reliable_index);
    if (is_sequenced(reliability)) write_u24_le(out, sequence_index);
    if (is_ordered(reliability)) {
        write_u24_le(out, order_index);
        out.push_back(order_channel);
    }
    if (split) {
        put_be32(out, split_count);
        put_be16(out, split_id);
        put_be32(out, split_index);
    }
    out.insert(out.end(), payload.begin(), payload.end());

    const auto expected = frame_header_size(reliability, split) + payload.size();
    if (out.size() != expected) {
        if (error) *error = "RakNet frame length accounting error";
        return false;
    }
    return true;
}

bool RakNetReliabilityEngine::queue(std::vector<std::uint8_t> payload,
                                    RakNetReliability reliability,
                                    std::uint8_t order_channel,
                                    std::string* error) {
    if (terminal_) {
        if (error) *error = last_error_.empty() ? "RakNet reliability engine is terminal" : last_error_;
        return false;
    }
    if (payload.empty()) {
        if (error) *error = "cannot queue empty RakNet payload";
        return false;
    }
    if (static_cast<std::uint8_t>(reliability) > static_cast<std::uint8_t>(RakNetReliability::ReliableSequenced)) {
        if (error) *error = "RakNet ACK-receipt reliability modes are not enabled in this profile";
        return false;
    }
    if (is_ordered(reliability) && order_channel >= config_.max_order_channels) {
        if (error) *error = "invalid RakNet order channel";
        return false;
    }
    const auto frame_overhead = frame_header_size(reliability, true);
    const auto fixed_overhead = kFrameSetHeaderBytes + frame_overhead;
    if (config_.mtu <= fixed_overhead || config_.max_datagram_bytes <= fixed_overhead) {
        if (error) *error = "RakNet MTU too small for a frame";
        return false;
    }
    if (is_reliable(reliability) && outstanding_.size() >= config_.max_outstanding_reliable) {
        if (error) *error = "RakNet reliable window full";
        return false;
    }

    const auto max_payload = std::min<std::size_t>(
        kMaxFrameBodyBytes,
        std::min<std::size_t>(config_.mtu - fixed_overhead, config_.max_datagram_bytes - fixed_overhead));
    if (max_payload == 0) {
        if (error) *error = "RakNet MTU too small for a frame";
        return false;
    }

    const bool fragmented = payload.size() > max_payload;
    const std::uint64_t split_count64 = fragmented ?
        (static_cast<std::uint64_t>(payload.size()) + max_payload - 1u) / max_payload : 1u;
    if (split_count64 > config_.max_split_count || split_count64 > std::numeric_limits<std::uint32_t>::max()) {
        if (error) *error = "RakNet payload requires too many split fragments";
        return false;
    }
    const auto split_count = static_cast<std::uint32_t>(split_count64);
    if (is_reliable(reliability) &&
        split_count > config_.max_outstanding_reliable - std::min(outstanding_.size(), config_.max_outstanding_reliable)) {
        if (error) *error = "RakNet payload exceeds reliable window capacity";
        return false;
    }
    if (split_count > config_.max_pending_datagrams - std::min(outbound_.size(), config_.max_pending_datagrams)) {
        if (error) *error = "RakNet payload exceeds outbound queue capacity";
        return false;
    }
    const std::uint16_t split_id = split_id_++;
    const auto order_index = is_ordered(reliability) ? next_order_indices_[order_channel] & kRakNetSequenceMask : 0;
    const auto sequence_index = is_sequenced(reliability) ? next_sequence_indices_[order_channel] & kRakNetSequenceMask : 0;
    if (is_ordered(reliability) && !is_sequenced(reliability)) {
        next_order_indices_[order_channel] = (next_order_indices_[order_channel] + 1u) & kRakNetSequenceMask;
        next_sequence_indices_[order_channel] = 0;
    } else if (is_sequenced(reliability)) {
        next_sequence_indices_[order_channel] = (next_sequence_indices_[order_channel] + 1u) & kRakNetSequenceMask;
    }

    for (std::uint32_t index = 0; index < split_count; ++index) {
        const std::size_t offset = static_cast<std::size_t>(index) * max_payload;
        const std::size_t length = std::min(max_payload, payload.size() - offset);
        std::vector<std::uint8_t> fragment(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                                           payload.begin() + static_cast<std::ptrdiff_t>(offset + length));
        const auto reliable_index = is_reliable(reliability) ? next_reliable_index_++ & kRakNetSequenceMask : 0;
        const auto frame_sequence_index = sequence_index;
        const auto datagram_sequence = next_datagram_sequence_++ & kRakNetSequenceMask;
        std::vector<std::uint8_t> frame;
        if (!encode_message_frame(fragment, reliability, order_channel, reliable_index, frame_sequence_index,
                                  order_index, fragmented, split_count, split_id, index, frame, error)) {
            return false;
        }
        std::vector<std::uint8_t> datagram;
        if (!build_frame_datagram(frame, datagram_sequence, datagram, error)) return false;
        if (!queue_datagram(datagram, datagram_sequence, is_reliable(reliability), error)) return false;
        if (is_reliable(reliability)) {
            auto [it, inserted] = outstanding_.emplace(datagram_sequence, Outstanding{});
            if (!inserted) {
                if (error) *error = "RakNet datagram sequence collision in reliable window";
                return false;
            }
            it->second.packet = outbound_.back();
            it->second.queued = true;
        }
    }
    return true;
}

std::optional<PendingDatagram> RakNetReliabilityEngine::next_outbound() {
    if (outbound_.empty()) return std::nullopt;
    auto packet = std::move(outbound_.front());
    outbound_.erase(outbound_.begin());
    if (packet.reliable) {
        auto it = outstanding_.find(packet.sequence & kRakNetSequenceMask);
        if (it != outstanding_.end()) it->second.queued = false;
    }
    return packet;
}

void RakNetReliabilityEngine::requeue_front(PendingDatagram packet) {
    if (terminal_) return;
    outbound_.insert(outbound_.begin(), std::move(packet));
    if (outbound_.front().reliable) {
        auto it = outstanding_.find(outbound_.front().sequence & kRakNetSequenceMask);
        if (it != outstanding_.end()) it->second.queued = true;
    }
}

void RakNetReliabilityEngine::mark_sent(const PendingDatagram& packet, std::uint64_t now_ms) {
    if (!packet.reliable) return;
    auto it = outstanding_.find(packet.sequence & kRakNetSequenceMask);
    if (it == outstanding_.end()) return;
    it->second.last_sent_ms = now_ms;
    it->second.queued = false;
}

std::vector<std::uint8_t> RakNetReliabilityEngine::encode_ack_nack(
    std::uint8_t packet_id,
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& ranges) {
    std::vector<std::uint8_t> out;
    out.push_back(packet_id);
    const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(ranges.size(), 65535));
    put_be16(out, count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto start = ranges[i].first & kRakNetSequenceMask;
        const auto end = ranges[i].second & kRakNetSequenceMask;
        out.push_back(start == end ? 1 : 0);
        write_u24_le(out, start);
        if (start != end) write_u24_le(out, end);
    }
    return out;
}

void RakNetReliabilityEngine::add_ack(std::uint32_t sequence) {
    add_linear_range(pending_acks_, sequence, kMaxAckNackRecords);
}

void RakNetReliabilityEngine::add_nack_range(std::uint32_t start, std::uint32_t end) {
    start &= kRakNetSequenceMask;
    end &= kRakNetSequenceMask;
    if (start <= end) {
        for (std::uint32_t v = start;; ++v) {
            add_linear_range(pending_nacks_, v, kMaxAckNackRecords);
            if (v == end) break;
        }
        return;
    }
    add_nack_range(start, kRakNetSequenceMask);
    add_nack_range(0, end);
}

void RakNetReliabilityEngine::remove_nack(std::uint32_t sequence) {
    remove_linear_value(pending_nacks_, sequence);
}

void RakNetReliabilityEngine::mark_received_sequence(std::uint32_t sequence) {
    sequence &= kRakNetSequenceMask;
    add_ack(sequence);
    if (!receive_sequence_initialized_) {
        receive_sequence_initialized_ = true;
        receive_next_sequence_ = (sequence + 1u) & kRakNetSequenceMask;
        return;
    }

    if (sequence == receive_next_sequence_) {
        remove_nack(sequence);
        receive_next_sequence_ = (receive_next_sequence_ + 1u) & kRakNetSequenceMask;
        for (;;) {
            auto it = receive_future_.find(receive_next_sequence_);
            if (it == receive_future_.end()) break;
            receive_future_.erase(it);
            remove_nack(receive_next_sequence_);
            receive_next_sequence_ = (receive_next_sequence_ + 1u) & kRakNetSequenceMask;
        }
        return;
    }

    if (seq_newer(sequence, receive_next_sequence_)) {
        const auto gap = (sequence - receive_next_sequence_) & kRakNetSequenceMask;
        const auto max_window = static_cast<std::uint32_t>(std::min<std::size_t>(
            std::max<std::size_t>(config_.max_reorder_frames_per_channel, 1) * 8u, 1u << 20));
        if (gap > max_window) {
            fail("RakNet sequence gap exceeds supported window");
            return;
        }
        receive_future_[sequence] = true;
        if (gap > 0) add_nack_range(receive_next_sequence_, (sequence - 1u) & kRakNetSequenceMask);
    }
}

bool RakNetReliabilityEngine::handle_ack_nack(const std::vector<std::uint8_t>& datagram,
                                              bool is_ack_packet,
                                              std::string* error) {
    if (datagram.size() > config_.mtu || datagram.size() > config_.max_datagram_bytes) {
        if (error) *error = "RakNet ACK/NACK exceeds configured MTU";
        return false;
    }
    std::size_t p = 1;
    std::uint16_t count = 0;
    if (!take_be16(datagram, p, count)) {
        if (error) *error = "RakNet ACK/NACK record count truncated";
        return false;
    }
    if (count > 1024) {
        if (error) *error = "RakNet ACK/NACK record count exceeds limit";
        return false;
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        if (p >= datagram.size()) {
            if (error) *error = "RakNet ACK/NACK record truncated";
            return false;
        }
        const bool single = datagram[p++] != 0;
        bool ok = false;
        const auto start = read_u24_le(datagram, p, &ok);
        if (!ok) {
            if (error) *error = "RakNet ACK/NACK start sequence truncated";
            return false;
        }
        std::uint32_t end = start;
        if (!single) {
            end = read_u24_le(datagram, p, &ok);
            if (!ok) {
                if (error) *error = "RakNet ACK/NACK end sequence truncated";
                return false;
            }
        }
        if (end < start) {
            if (error) *error = "RakNet ACK/NACK range is not representable as a linear range";
            return false;
        }
        // Ranges are capped by the encoded record count and by the sender's bounded window.
        for (std::uint32_t seq = start;; ++seq) {
            const auto normalized = seq & kRakNetSequenceMask;
            if (is_ack_packet) {
                outstanding_.erase(normalized);
            } else {
                auto it = outstanding_.find(normalized);
                if (it != outstanding_.end() && !it->second.queued) {
                    if (outbound_.size() >= config_.max_pending_datagrams) {
                        if (error) *error = "RakNet resend queue full";
                        return false;
                    }
                    it->second.queued = true;
                    outbound_.push_back(it->second.packet);
                }
            }
            if (normalized == (end & kRakNetSequenceMask)) break;
            if (seq == std::numeric_limits<std::uint32_t>::max()) break;
        }
    }
    if (p != datagram.size()) {
        if (error) *error = "RakNet ACK/NACK has trailing bytes";
        return false;
    }
    return true;
}

bool RakNetReliabilityEngine::note_reliable_received(std::uint32_t reliable_index) {
    reliable_index &= kRakNetSequenceMask;
    if (received_reliable_indices_.find(reliable_index) != received_reliable_indices_.end()) return false;
    received_reliable_indices_.insert(reliable_index);
    received_reliable_order_.push_back(reliable_index);
    prune_received_reliable_history();
    return true;
}

void RakNetReliabilityEngine::prune_received_reliable_history() {
    const auto max_history = std::max<std::size_t>(config_.max_received_reliable_history, 1);
    while (received_reliable_order_.size() > max_history) {
        const auto old = received_reliable_order_.front();
        received_reliable_order_.pop_front();
        received_reliable_indices_.erase(old);
    }
}

bool RakNetReliabilityEngine::parse_frame(const std::vector<std::uint8_t>& datagram,
                                          std::size_t& p,
                                          std::uint32_t datagram_sequence,
                                          std::uint64_t now_ms,
                                          ReceivedPayload& out,
                                          std::string* error) {
    if (p + 3 > datagram.size()) {
        if (error) *error = "RakNet frame header truncated";
        return false;
    }
    const auto flags = datagram[p++];
    const auto reliability = static_cast<RakNetReliability>((flags >> 5) & 0x07);
    if (static_cast<std::uint8_t>(reliability) > static_cast<std::uint8_t>(RakNetReliability::ReliableSequenced)) {
        if (error) *error = "unsupported RakNet reliability type";
        return false;
    }
    const bool split = (flags & 0x10u) != 0;
    std::uint16_t bit_length = 0;
    if (!take_be16(datagram, p, bit_length) || (bit_length % 8u) != 0) {
        if (error) *error = "RakNet frame length is not byte aligned";
        return false;
    }
    const std::size_t body_length = bit_length / 8u;
    std::uint32_t reliable_index = 0;
    std::uint32_t sequence_index = 0;
    std::uint32_t order_index = 0;
    std::uint8_t order_channel = 0;
    bool ok = false;
    if (is_reliable(reliability)) {
        reliable_index = read_u24_le(datagram, p, &ok);
        if (!ok) { if (error) *error = "RakNet reliable index truncated"; return false; }
    }
    if (is_sequenced(reliability)) {
        sequence_index = read_u24_le(datagram, p, &ok);
        if (!ok) { if (error) *error = "RakNet sequence index truncated"; return false; }
    }
    if (is_ordered(reliability)) {
        order_index = read_u24_le(datagram, p, &ok);
        if (!ok || p >= datagram.size()) { if (error) *error = "RakNet order metadata truncated"; return false; }
        order_channel = datagram[p++];
        if (order_channel >= config_.max_order_channels) {
            if (error) *error = "RakNet order channel out of range";
            return false;
        }
    }

    std::uint32_t split_count = 0;
    std::uint16_t split_id = 0;
    std::uint32_t split_index = 0;
    if (split) {
        if (!take_be32(datagram, p, split_count) || !take_be16(datagram, p, split_id) ||
            !take_be32(datagram, p, split_index)) {
            if (error) *error = "RakNet split metadata truncated";
            return false;
        }
        if (split_count == 0 || split_count > config_.max_split_count || split_index >= split_count) {
            if (error) *error = "invalid RakNet split metadata";
            return false;
        }
    }
    if (body_length > kMaxFrameBodyBytes || p + body_length > datagram.size()) {
        if (error) *error = "RakNet frame body exceeds datagram";
        return false;
    }

    std::vector<std::uint8_t> body(datagram.begin() + static_cast<std::ptrdiff_t>(p),
                                   datagram.begin() + static_cast<std::ptrdiff_t>(p + body_length));
    p += body_length;

    if (is_reliable(reliability) && !note_reliable_received(reliable_index)) {
        out = {};
        return true; // Duplicate reliable frame. The datagram is still ACKed.
    }

    out.payload = std::move(body);
    out.reliability = reliability;
    out.order_channel = order_channel;
    out.datagram_sequence = datagram_sequence & kRakNetSequenceMask;
    out.reliable_index = reliable_index;
    out.sequence_index = sequence_index;
    out.order_index = order_index;

    if (!split) return true;

    auto it = reassembly_.find(split_id);
    if (it == reassembly_.end()) {
        if (reassembly_.size() >= config_.max_reassembly_packets) {
            if (error) *error = "RakNet reassembly packet limit exceeded";
            return false;
        }
        if (reassembly_bytes_ + body_length > config_.max_reassembly_bytes) {
            if (error) *error = "RakNet reassembly byte limit exceeded";
            return false;
        }
        Reassembly r;
        r.count = split_count;
        r.fragments.resize(split_count);
        r.reliability = reliability;
        r.order_channel = order_channel;
        r.reliable_index = reliable_index;
        r.sequence_index = sequence_index;
        r.order_index = order_index;
        r.expires_ms = now_ms + config_.reassembly_timeout_ms;
        it = reassembly_.emplace(split_id, std::move(r)).first;
    }

    auto& r = it->second;
    if (r.count != split_count || r.reliability != reliability || r.order_channel != order_channel ||
        r.order_index != order_index) {
        if (error) *error = "RakNet split metadata changed within a compound packet";
        return false;
    }
    if (!r.fragments[split_index].has_value()) {
        if (reassembly_bytes_ + body_length > config_.max_reassembly_bytes) {
            if (error) *error = "RakNet reassembly byte limit exceeded";
            return false;
        }
        reassembly_bytes_ += body_length;
        r.bytes += body_length;
        r.fragments[split_index] = std::move(out.payload);
    }

    for (const auto& f : r.fragments) {
        if (!f.has_value()) {
            out.payload.clear();
            return true;
        }
    }

    out.payload.clear();
    out.payload.reserve(r.bytes);
    for (auto& f : r.fragments) out.payload.insert(out.payload.end(), f->begin(), f->end());
    out.reliable_index = r.reliable_index;
    out.sequence_index = r.sequence_index;
    out.order_index = r.order_index;
    reassembly_bytes_ -= r.bytes;
    reassembly_.erase(it);
    return true;
}

void RakNetReliabilityEngine::emit_ordered(ReceivedPayload payload, const DeliverHandler& deliver) {
    if (payload.order_channel >= order_states_.size()) {
        fail("RakNet ordered payload channel out of range");
        return;
    }
    auto& state = order_states_[payload.order_channel];
    if (!state.initialized) {
        state.initialized = true;
        state.next_expected = 0;
    }

    const auto index = payload.order_index & kRakNetSequenceMask;
    if (index == state.next_expected) {
        if (deliver) deliver(payload);
        state.next_expected = (state.next_expected + 1u) & kRakNetSequenceMask;
        for (;;) {
            auto it = state.buffered.find(state.next_expected);
            if (it == state.buffered.end()) break;
            auto next = std::move(it->second);
            state.buffered.erase(it);
            if (deliver) deliver(next);
            state.next_expected = (state.next_expected + 1u) & kRakNetSequenceMask;
        }
        return;
    }

    if (seq_newer(index, state.next_expected)) {
        if (state.buffered.size() >= config_.max_reorder_frames_per_channel) {
            fail("RakNet ordered receive window exhausted");
            return;
        }
        auto it = state.buffered.find(index);
        if (it == state.buffered.end()) {
            state.buffered.emplace(index, std::move(payload));
        } else if (is_sequenced(payload.reliability) &&
                   seq_newer(payload.sequence_index, it->second.sequence_index)) {
            it->second = std::move(payload);
        }
    }
}

bool RakNetReliabilityEngine::handle_payload(ReceivedPayload payload,
                                             std::uint64_t now_ms,
                                             const DeliverHandler& deliver,
                                             std::string* error) {
    (void)now_ms;
    (void)error;
    if (payload.payload.empty()) return true;
    if (is_ordered(payload.reliability)) {
        emit_ordered(std::move(payload), deliver);
        return !terminal_;
    }
    if (deliver) deliver(payload);
    return true;
}

bool RakNetReliabilityEngine::ingest(const std::vector<std::uint8_t>& datagram,
                                     std::uint64_t now_ms,
                                     const DeliverHandler& deliver,
                                     std::string* error) {
    if (terminal_) {
        if (error) *error = last_error_.empty() ? "RakNet reliability engine is terminal" : last_error_;
        return false;
    }
    if (datagram.empty()) return true;
    if (datagram.size() > config_.max_datagram_bytes || datagram.size() > config_.mtu) {
        if (error) *error = "received RakNet datagram exceeds configured MTU";
        return false;
    }

    const auto id = datagram[0];
    if (id == kAck || id == kNack) return handle_ack_nack(datagram, id == kAck, error);
    if (id < kFrameSetMin || id > kFrameSetMax) return true;
    if (datagram.size() < kFrameSetHeaderBytes) {
        if (error) *error = "RakNet frame set truncated";
        return false;
    }

    std::size_t p = 1;
    bool ok = false;
    const auto sequence = read_u24_le(datagram, p, &ok);
    if (!ok) {
        if (error) *error = "RakNet datagram sequence truncated";
        return false;
    }
    mark_received_sequence(sequence);
    if (terminal_) {
        if (error) *error = last_error_;
        return false;
    }

    while (p < datagram.size()) {
        ReceivedPayload payload;
        if (!parse_frame(datagram, p, sequence, now_ms, payload, error)) return false;
        if (payload.payload.empty()) continue;
        if (!handle_payload(std::move(payload), now_ms, deliver, error)) return false;
        if (terminal_) {
            if (error) *error = last_error_;
            return false;
        }
    }
    return true;
}

void RakNetReliabilityEngine::emit_pending_control_packets() {
    if (outbound_.size() >= config_.max_pending_datagrams) return;
    const auto max_records_per_packet = std::max<std::size_t>(
        1, std::min<std::size_t>(kMaxAckNackRecords, (config_.mtu > static_cast<std::uint16_t>(3) ? static_cast<std::size_t>((config_.mtu - static_cast<std::uint16_t>(3)) / static_cast<std::uint16_t>(7)) : static_cast<std::size_t>(0))));
    auto emit = [&](std::uint8_t id, std::vector<std::pair<std::uint32_t, std::uint32_t>>& ranges) {
        while (!ranges.empty() && outbound_.size() < config_.max_pending_datagrams) {
            const auto count = std::min(max_records_per_packet, ranges.size());
            std::vector<std::pair<std::uint32_t, std::uint32_t>> chunk(ranges.begin(), ranges.begin() + static_cast<std::ptrdiff_t>(count));
            ranges.erase(ranges.begin(), ranges.begin() + static_cast<std::ptrdiff_t>(count));
            outbound_.push_back(PendingDatagram{encode_ack_nack(id, chunk), 0, false});
        }
    };
    emit(kNack, pending_nacks_);
    emit(kAck, pending_acks_);
}

void RakNetReliabilityEngine::tick(std::uint64_t now_ms) {
    if (terminal_) return;
    last_tick_ms_ = now_ms;

    for (auto it = outstanding_.begin(); it != outstanding_.end(); ++it) {
        auto& record = it->second;
        if (!record.queued && now_ms >= record.last_sent_ms && now_ms - record.last_sent_ms >= config_.resend_after_ms) {
            if (record.retries >= config_.max_retries) {
                fail("RakNet reliable datagram exceeded retry limit");
                return;
            }
            if (outbound_.size() >= config_.max_pending_datagrams) break;
            ++record.retries;
            record.queued = true;
            outbound_.push_back(record.packet);
        }
    }

    for (auto it = reassembly_.begin(); it != reassembly_.end();) {
        if (now_ms >= it->second.expires_ms) {
            reassembly_bytes_ -= it->second.bytes;
            it = reassembly_.erase(it);
        } else {
            ++it;
        }
    }

    emit_pending_control_packets();
}

} // namespace raknet
} // namespace bedrockai
