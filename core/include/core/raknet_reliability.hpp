#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace bedrockai {
namespace raknet {

enum class RakNetReliability : std::uint8_t {
    Unreliable = 0,
    UnreliableSequenced = 1,
    Reliable = 2,
    ReliableOrdered = 3,
    ReliableSequenced = 4,
    UnreliableWithAckReceipt = 5,
    ReliableWithAckReceipt = 6,
    ReliableOrderedWithAckReceipt = 7
};

constexpr std::uint32_t kRakNetSequenceMask = 0x00ffffffu;
constexpr std::uint8_t kFrameSetMin = 0x80;
constexpr std::uint8_t kFrameSetMax = 0x8d;
constexpr std::uint8_t kNack = 0xa0;
constexpr std::uint8_t kAck = 0xc0;

struct ReliabilityConfig {
    std::uint16_t mtu = 1492;
    std::size_t max_datagram_bytes = 2048;
    std::size_t max_pending_datagrams = 512;
    std::size_t max_outstanding_reliable = 256;
    std::size_t max_reorder_frames_per_channel = 256;
    std::size_t max_reassembly_packets = 64;
    std::size_t max_reassembly_bytes = 4 * 1024 * 1024;
    std::uint32_t max_split_count = 1024;
    std::uint64_t resend_after_ms = 250;
    std::uint32_t max_retries = 8;
    std::uint64_t reassembly_timeout_ms = 10'000;
    std::uint8_t max_order_channels = 32;
    std::size_t max_received_reliable_history = 4096;
};

struct PendingDatagram {
    std::vector<std::uint8_t> bytes;
    std::uint32_t sequence = 0;
    bool reliable = false;
};

struct ReceivedPayload {
    std::vector<std::uint8_t> payload;
    RakNetReliability reliability = RakNetReliability::Unreliable;
    std::uint8_t order_channel = 0;
    std::uint32_t datagram_sequence = 0;
    std::uint32_t reliable_index = 0;
    std::uint32_t sequence_index = 0;
    std::uint32_t order_index = 0;
};

class RakNetReliabilityEngine {
public:
    using DeliverHandler = std::function<void(const ReceivedPayload&)>;

    explicit RakNetReliabilityEngine(ReliabilityConfig config = {});

    bool queue(std::vector<std::uint8_t> payload,
               RakNetReliability reliability = RakNetReliability::ReliableOrdered,
               std::uint8_t order_channel = 0,
               std::string* error = nullptr);

    std::optional<PendingDatagram> next_outbound();
    void requeue_front(PendingDatagram packet);
    void mark_sent(const PendingDatagram& packet, std::uint64_t now_ms);

    bool ingest(const std::vector<std::uint8_t>& datagram,
                std::uint64_t now_ms,
                const DeliverHandler& deliver,
                std::string* error = nullptr);

    void tick(std::uint64_t now_ms);

    bool terminal() const { return terminal_; }
    const std::string& last_error() const { return last_error_; }
    std::size_t pending_outbound() const { return outbound_.size(); }
    std::size_t outstanding_reliable() const { return outstanding_.size(); }
    std::uint32_t next_datagram_sequence() const { return next_datagram_sequence_; }
    std::uint32_t next_reliable_index() const { return next_reliable_index_; }
    std::uint32_t next_order_index(std::uint8_t channel) const;

    static std::uint32_t read_u24_le(const std::vector<std::uint8_t>& b, std::size_t& p, bool* ok = nullptr);
    static void write_u24_le(std::vector<std::uint8_t>& b, std::uint32_t value);
    static std::vector<std::uint8_t> encode_ack_nack(
        std::uint8_t packet_id,
        const std::vector<std::pair<std::uint32_t, std::uint32_t>>& ranges);

private:
    struct Outstanding {
        PendingDatagram packet;
        std::uint64_t last_sent_ms = 0;
        std::uint32_t retries = 0;
        bool queued = true;
    };

    struct OrderState {
        std::uint32_t next_expected = 0;
        std::map<std::uint32_t, ReceivedPayload> buffered;
        bool initialized = false;
    };

    struct SequencedState {
        std::uint32_t order_index = 0;
        std::uint32_t sequence_index = 0;
        bool initialized = false;
    };

    struct Reassembly {
        std::uint32_t count = 0;
        std::vector<std::optional<std::vector<std::uint8_t>>> fragments;
        std::size_t bytes = 0;
        RakNetReliability reliability = RakNetReliability::Unreliable;
        std::uint8_t order_channel = 0;
        std::uint32_t reliable_index = 0;
        std::uint32_t sequence_index = 0;
        std::uint32_t order_index = 0;
        std::uint64_t expires_ms = 0;
    };

    ReliabilityConfig config_;
    std::uint32_t next_datagram_sequence_ = 0;
    std::uint32_t next_reliable_index_ = 0;
    std::vector<std::uint32_t> next_order_indices_;
    std::vector<std::uint32_t> next_sequence_indices_;
    std::vector<OrderState> order_states_;
    std::vector<SequencedState> sequenced_states_;
    std::set<std::uint32_t> received_reliable_indices_;
    std::deque<std::uint32_t> received_reliable_order_;
    std::map<std::uint32_t, Outstanding> outstanding_;
    std::vector<PendingDatagram> outbound_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pending_acks_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pending_nacks_;
    std::map<std::uint32_t, Reassembly> reassembly_;
    std::size_t reassembly_bytes_ = 0;
    std::uint32_t receive_next_sequence_ = 0;
    bool receive_sequence_initialized_ = false;
    std::map<std::uint32_t, bool> receive_future_;
    std::uint64_t last_tick_ms_ = 0;
    bool terminal_ = false;
    std::string last_error_;
    std::uint16_t split_id_ = 0;

    void fail(const char* message);
    bool queue_datagram(std::vector<std::uint8_t> bytes, std::uint32_t sequence, bool reliable,
                        std::string* error);
    bool build_frame_datagram(const std::vector<std::uint8_t>& frame,
                              std::uint32_t sequence,
                              std::vector<std::uint8_t>& datagram,
                              std::string* error) const;
    bool encode_message_frame(const std::vector<std::uint8_t>& payload,
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
                              std::string* error) const;

    bool parse_frame(const std::vector<std::uint8_t>& datagram,
                     std::size_t& p,
                     std::uint32_t datagram_sequence,
                     std::uint64_t now_ms,
                     ReceivedPayload& out,
                     std::string* error);
    bool handle_payload(ReceivedPayload payload,
                        std::uint64_t now_ms,
                        const DeliverHandler& deliver,
                        std::string* error);
    bool handle_ack_nack(const std::vector<std::uint8_t>& datagram,
                         bool is_ack,
                         std::string* error);
    bool note_reliable_received(std::uint32_t reliable_index);
    void prune_received_reliable_history();
    void mark_received_sequence(std::uint32_t sequence);
    void add_ack(std::uint32_t sequence);
    void add_nack_range(std::uint32_t start, std::uint32_t end);
    void remove_nack(std::uint32_t sequence);
    void emit_ordered(ReceivedPayload payload, const DeliverHandler& deliver);
    void emit_pending_control_packets();
    static bool is_reliable(RakNetReliability reliability);
    static bool is_sequenced(RakNetReliability reliability);
    static bool is_ordered(RakNetReliability reliability);
    static std::uint32_t normalize24(std::uint32_t value) { return value & kRakNetSequenceMask; }
    static bool seq_newer(std::uint32_t a, std::uint32_t b);
    static std::size_t frame_header_size(RakNetReliability reliability, bool split);
};

} // namespace raknet
} // namespace bedrockai
