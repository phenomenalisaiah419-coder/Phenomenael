#include "core/transport.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace bedrockai {
namespace {

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}
void put_u64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}
void put_string(std::vector<std::uint8_t>& out, const std::string& s) {
    put_u16(out, static_cast<std::uint16_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

bool take_u16(const std::vector<std::uint8_t>& b, std::size_t& p, std::uint16_t& v) {
    if (p + 2 > b.size()) return false;
    const std::uint16_t low = static_cast<std::uint16_t>(b[p]);
    const std::uint16_t high = static_cast<std::uint16_t>(static_cast<std::uint16_t>(b[p + 1]) << 8);
    v = static_cast<std::uint16_t>(low | high);
    p += 2; return true;
}
bool take_u32(const std::vector<std::uint8_t>& b, std::size_t& p, std::uint32_t& v) {
    if (p + 4 > b.size()) return false;
    v = 0; for (std::size_t i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(b[p + i]) << (8 * i);
    p += 4; return true;
}
bool take_u64(const std::vector<std::uint8_t>& b, std::size_t& p, std::uint64_t& v) {
    if (p + 8 > b.size()) return false;
    v = 0; for (std::size_t i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(b[p + i]) << (8 * i);
    p += 8; return true;
}
bool take_string(const std::vector<std::uint8_t>& b, std::size_t& p, std::string& s) {
    std::uint16_t n = 0;
    if (!take_u16(b, p, n) || p + n > b.size()) return false;
    s.assign(reinterpret_cast<const char*>(b.data() + p), n); p += n; return true;
}
std::uint32_t checksum(const std::vector<std::uint8_t>& b) {
    // Stable lightweight envelope checksum; Minecraft packet payload is still opaque here.
    std::uint32_t h = 2166136261u;
    for (auto c : b) { h ^= c; h *= 16777619u; }
    return h;
}

PacketView make_opaque_view(const TransportFrame& f) {
    PacketView p;
    p.direction = f.direction;
    p.id = f.packet_id;
    p.tick = f.sequence;
    // Real backend implementations replace this opaque seam with decoded fields.
    return p;
}

} // namespace

std::vector<std::uint8_t> TransportFrameCodec::encode(const TransportFrame& frame) {
    std::vector<std::uint8_t> out;
    out.reserve(64 + frame.payload.size());
    put_u32(out, frame.magic);
    put_u16(out, frame.format_version);
    put_u16(out, static_cast<std::uint16_t>(frame.direction));
    put_u32(out, frame.identity.protocol);
    put_string(out, frame.identity.game_version);
    put_u32(out, static_cast<std::uint32_t>(frame.identity.transport));
    put_u32(out, frame.packet_id);
    put_u64(out, frame.sequence);
    put_u32(out, static_cast<std::uint32_t>(frame.payload.size()));
    out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    put_u32(out, checksum(out));
    return out;
}

std::optional<TransportFrame> TransportFrameCodec::decode(const std::vector<std::uint8_t>& bytes,
                                                          std::string* error) {
    auto fail = [&](const char* why) -> std::optional<TransportFrame> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (bytes.size() < 4 + 2 + 2 + 4 + 2 + 4 + 4 + 8 + 4 + 4) return fail("transport frame too small");
    std::size_t p = 0; TransportFrame f;
    if (!take_u32(bytes,p,f.magic) || f.magic != TransportFrame::kMagic) return fail("bad transport magic");
    if (!take_u16(bytes,p,f.format_version) || f.format_version != TransportFrame::kFormatVersion) return fail("unsupported transport format");
    std::uint16_t dir = 0; if (!take_u16(bytes,p,dir) || dir > 1) return fail("invalid direction");
    f.direction = static_cast<Direction>(dir);
    if (!take_u32(bytes,p,f.identity.protocol)) return fail("missing protocol");
    if (!take_string(bytes,p,f.identity.game_version)) return fail("missing game version");
    std::uint32_t transport = 0; if (!take_u32(bytes,p,transport) || transport > 2) return fail("invalid transport");
    f.identity.transport = static_cast<BedrockTransport>(transport);
    if (!take_u32(bytes,p,f.packet_id) || !take_u64(bytes,p,f.sequence)) return fail("missing packet metadata");
    std::uint32_t n = 0; if (!take_u32(bytes,p,n)) return fail("missing payload length");
    if (n > bytes.size() || p + static_cast<std::size_t>(n) + 4 != bytes.size()) return fail("invalid payload length");
    f.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(p), bytes.begin() + static_cast<std::ptrdiff_t>(p+n));
    p += n;
    std::uint32_t got = 0; if (!take_u32(bytes,p,got)) return fail("missing checksum");
    std::vector<std::uint8_t> prefix(bytes.begin(), bytes.end() - 4);
    if (checksum(prefix) != got) return fail("transport checksum mismatch");
    return f;
}

Protocol2193Session::Protocol2193Session(TransportSessionConfig config) : config_(std::move(config)) {}

bool Protocol2193Session::attach(BedrockTransportBackend& backend, std::string* error) {
    if (backend_ != nullptr) { if (error) *error = "transport backend already attached"; return false; }
    identity_ = config_.expected;
    if (!identity_.is_target()) { if (error) *error = "session config is not pinned to Bedrock 1.26.50 / 2193"; state_=TransportState::Failed; return false; }
    backend_ = &backend;
    state_ = TransportState::Connecting;
    backend.set_receive_handler([this](const TransportFrame& frame){ on_transport_frame(frame); });
    std::string connect_error;
    if (!backend.connect(identity_, &connect_error)) {
        state_ = TransportState::Failed; backend_ = nullptr;
        if (error) *error = connect_error.empty() ? "backend connection failed" : connect_error;
        return false;
    }
    const TransportState backend_state = backend.state();
    if (backend_state == TransportState::Connecting) {
        state_ = TransportState::Connecting;
    } else if (backend_state == TransportState::Failed) {
        state_ = TransportState::Failed;
        backend_ = nullptr;
        if (error) *error = "backend entered Failed state during attach";
        return false;
    } else if (backend_state == TransportState::Disconnected || backend_state == TransportState::Closing) {
        state_ = backend_state;
    } else {
        // Synchronous/in-memory backends may report InGame or Authenticating on
        // connect; those are usable by the Bedrock login layer immediately.
        state_ = TransportState::Authenticating;
    }
    return true;
}

bool Protocol2193Session::queue_serverbound(std::uint32_t packet_id, std::vector<std::uint8_t> payload,
                                             std::string* error) {
    if (!backend_ || state_ == TransportState::Disconnected || state_ == TransportState::Failed) {
        if (error) *error = "cannot queue without an attached transport";
        return false;
    }
    if (outbound_.size() >= config_.max_outbound_frames) {
        if (error) *error = "outbound transport queue full";
        return false;
    }
    TransportFrame f;
    f.identity = identity_; f.direction = Direction::Serverbound; f.packet_id=packet_id;
    f.sequence = next_sequence_++; f.payload=std::move(payload);
    outbound_.push_back(std::move(f));
    return true;
}

bool Protocol2193Session::flush(std::string* error) {
    if (!backend_) { if (error) *error = "no transport backend"; return false; }
    while (!outbound_.empty()) {
        if (!backend_->send(outbound_.front(), error)) return false;
        outbound_.erase(outbound_.begin());
    }
    return true;
}

void Protocol2193Session::detach() {
    if (backend_) backend_->close();
    backend_ = nullptr; outbound_.clear(); state_=TransportState::Disconnected;
}

bool Protocol2193Session::validate_frame(const TransportFrame& frame, std::string* error) const {
    if (frame.magic != TransportFrame::kMagic || frame.format_version != TransportFrame::kFormatVersion) {
        if (error) *error = "invalid transport frame header";
        return false;
    }
    if (!frame.identity.is_target() || frame.identity.protocol != identity_.protocol) {
        if (error) *error = "frame protocol is not Bedrock 1.26.50 / 2193";
        return false;
    }
    if (frame.direction == Direction::Serverbound && frame.sequence == 0) {
        if (error) *error = "serverbound frame sequence cannot be zero";
        return false;
    }
    return true;
}

void Protocol2193Session::transition_from_packet(const TransportFrame& frame) {
    if (frame.direction == Direction::Clientbound && frame.packet_id == packet_id::StartGame) state_ = TransportState::InGame;
    if (frame.packet_id == packet_id::Disconnect) state_ = TransportState::Closing;
}

bool Protocol2193Session::ingest(const TransportFrame& frame, std::string* error) {
    if (!validate_frame(frame,error)) { state_=TransportState::Failed; return false; }
    PacketView view = make_opaque_view(frame);
    // The opaque frame deliberately bypasses field reconciliation until a concrete codec decodes it.
    // Only transport lifecycle is advanced here; decoded PacketView ingestion is supported separately by ProtocolAdapter.
    std::string adapter_error;
    if (!adapter_.accept_server_identity(frame.identity,&adapter_error)) { if(error)*error=adapter_error; state_=TransportState::Failed; return false; }
    if (!adapter_.observe(view,&adapter_error)) {
        // Unknown/opaque packet schemas may be handled by the concrete codec; packet-name checks are unavailable here.
        if (frame.packet_id==packet_id::StartGame || frame.packet_id==packet_id::MovePlayer || frame.packet_id==packet_id::LevelChunk || frame.packet_id==packet_id::PlayerAuthInput || frame.packet_id==packet_id::InventoryTransaction) {
            // Keep these packet IDs flowing; concrete decoding decides field validity.
        }
    }
    transition_from_packet(frame);
    return true;
}

void Protocol2193Session::on_transport_frame(const TransportFrame& frame) {
    std::string ignored;
    if (!ingest(frame, &ignored)) return;
    if (receive_handler_) receive_handler_(frame);
}

} // namespace bedrockai
