#include "core/raknet_reliability.hpp"
#include "core/raknet_transport.hpp"
#include "core/socket_datagram.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace bedrockai;
using namespace bedrockai::raknet;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++failures; } } while (0)

static std::vector<std::uint8_t> make_protocol_ipv4(std::uint8_t a, std::uint8_t b,
                                                    std::uint8_t c, std::uint8_t d,
                                                    std::uint16_t port) {
    std::vector<std::uint8_t> out{0x04,
                                  static_cast<std::uint8_t>(~a), static_cast<std::uint8_t>(~b),
                                  static_cast<std::uint8_t>(~c), static_cast<std::uint8_t>(~d),
                                  static_cast<std::uint8_t>(port >> 8), static_cast<std::uint8_t>(port)};
    return out;
}

static std::vector<std::uint8_t> make_reply1(std::uint64_t guid, std::uint16_t mtu) {
    std::vector<std::uint8_t> out{offline::OpenConnectionReply1};
    const auto& magic = offline_magic();
    out.insert(out.end(), magic.begin(), magic.end());
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<std::uint8_t>((guid >> (8 * i)) & 0xff));
    out.push_back(0);
    out.push_back(static_cast<std::uint8_t>(mtu >> 8));
    out.push_back(static_cast<std::uint8_t>(mtu));
    return out;
}

static std::vector<std::uint8_t> make_reply2(std::uint64_t guid, std::uint16_t mtu, std::uint16_t port) {
    std::vector<std::uint8_t> out{offline::OpenConnectionReply2};
    const auto& magic = offline_magic();
    out.insert(out.end(), magic.begin(), magic.end());
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<std::uint8_t>((guid >> (8 * i)) & 0xff));
    const auto address = make_protocol_ipv4(127, 0, 0, 1, port);
    out.insert(out.end(), address.begin(), address.end());
    out.push_back(static_cast<std::uint8_t>(mtu >> 8));
    out.push_back(static_cast<std::uint8_t>(mtu));
    out.push_back(0);
    return out;
}

static std::vector<std::uint8_t> make_connection_accepted(std::uint64_t request_timestamp,
                                                           std::uint64_t accepted_timestamp,
                                                           std::uint16_t port) {
    auto address = make_protocol_ipv4(127, 0, 0, 1, port);
    std::vector<std::uint8_t> out{offline::ConnectionRequestAccepted};
    out.insert(out.end(), address.begin(), address.end());
    out.push_back(0);
    out.push_back(0); // system index
    for (int i = 0; i < 10; ++i) out.insert(out.end(), address.begin(), address.end());
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<std::uint8_t>((request_timestamp >> (8 * i)) & 0xff));
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<std::uint8_t>((accepted_timestamp >> (8 * i)) & 0xff));
    return out;
}

static std::vector<std::uint8_t> take_last(const LoopbackDatagramIo& io) {
    CHECK(!io.outbound().empty(), "loopback outbound is non-empty");
    return io.outbound().empty() ? std::vector<std::uint8_t>{} : io.outbound().back();
}



static void test_ocr1() {
    auto request = OfflineMessageCodec::open_connection_request1(1492);
    CHECK(request.size() == 1492, "OCR1 exactly fills requested MTU");
    CHECK(request[0] == offline::OpenConnectionRequest1, "OCR1 ID");
    CHECK(request[17] == 11, "OCR1 RakNet wire protocol 11");
    CHECK(OfflineMessageCodec::is_offline_magic(request, 1), "OCR1 magic");

    auto smaller = OfflineMessageCodec::open_connection_request1(400);
    CHECK(smaller.size() == 400, "OCR1 smaller MTU exact size");
}

static void test_frame_and_ack() {
    ReliabilityConfig cfg;
    cfg.mtu = 500;
    cfg.max_datagram_bytes = 500;
    RakNetReliabilityEngine sender(cfg);
    RakNetReliabilityEngine receiver(cfg);
    std::string error;

    CHECK(sender.queue({0xde, 0xad, 0xbe, 0xef}, RakNetReliability::ReliableOrdered, 0, &error), "queue reliable ordered");
    auto packet = sender.next_outbound();
    CHECK(packet.has_value(), "reliable frame available");
    if (!packet) return;
    CHECK((*packet).bytes.size() <= 500, "frame fits MTU");
    CHECK((*packet).bytes[0] >= kFrameSetMin && (*packet).bytes[0] <= kFrameSetMax, "frame-set ID");

    std::vector<std::vector<std::uint8_t>> delivered;
    CHECK(receiver.ingest(packet->bytes, 100, [&](const ReceivedPayload& p) {
        delivered.push_back(p.payload);
        CHECK(p.reliability == RakNetReliability::ReliableOrdered, "decoded reliability");
        CHECK(p.order_index == 0, "decoded order index");
        CHECK(p.order_channel == 0, "decoded order channel");
    }, &error), "receiver ingests frame");
    CHECK(delivered.size() == 1 && delivered[0] == std::vector<std::uint8_t>({0xde, 0xad, 0xbe, 0xef}), "payload delivered once");

    receiver.tick(100);
    auto ack = receiver.next_outbound();
    CHECK(ack.has_value(), "receiver emitted ACK");
    if (ack) {
        CHECK((*ack).bytes[0] == kAck, "ACK packet ID");
        CHECK(sender.ingest(ack->bytes, 100, {}, &error), "sender consumes ACK");
    }
    sender.mark_sent(*packet, 100);
    CHECK(sender.outstanding_reliable() == 0, "ACK removes reliable outstanding");

    // Duplicate reliable datagrams must be ACKed but never redeliver application payload.
    delivered.clear();
    CHECK(receiver.ingest(packet->bytes, 110, [&](const ReceivedPayload& p) { delivered.push_back(p.payload); }, &error), "duplicate frame accepted");
    CHECK(delivered.empty(), "duplicate reliable frame suppressed");
    receiver.tick(110);
    CHECK(receiver.next_outbound().has_value(), "duplicate still receives ACK");
}

static void test_nack_and_resend() {
    ReliabilityConfig cfg;
    cfg.mtu = 500;
    cfg.max_datagram_bytes = 500;
    cfg.resend_after_ms = 20;
    cfg.max_retries = 2;
    RakNetReliabilityEngine sender(cfg);
    RakNetReliabilityEngine receiver(cfg);
    std::string error;

    CHECK(sender.queue({0x00}, RakNetReliability::ReliableOrdered, 0, &error), "queue 0");
    CHECK(sender.queue({0x01}, RakNetReliability::ReliableOrdered, 0, &error), "queue 1");
    CHECK(sender.queue({0x02}, RakNetReliability::ReliableOrdered, 0, &error), "queue 2");
    auto d0 = sender.next_outbound();
    auto d1 = sender.next_outbound();
    auto d2 = sender.next_outbound();
    CHECK(d0 && d1 && d2, "three reliable datagrams available");
    if (!d0 || !d1 || !d2) return;
    sender.mark_sent(*d0, 0);
    sender.mark_sent(*d1, 0);
    sender.mark_sent(*d2, 0);

    CHECK(receiver.ingest(d0->bytes, 1, {}, &error), "receiver gets seq0");
    CHECK(receiver.ingest(d2->bytes, 2, {}, &error), "receiver gets seq2 with gap");
    receiver.tick(2);

    std::vector<PendingDatagram> controls;
    while (auto p = receiver.next_outbound()) controls.push_back(std::move(*p));
    CHECK(controls.size() >= 2, "ACK and NACK emitted");
    bool saw_nack = false;
    bool saw_ack = false;
    for (const auto& control : controls) {
        saw_nack = saw_nack || control.bytes[0] == kNack;
        saw_ack = saw_ack || control.bytes[0] == kAck;
        CHECK(sender.ingest(control.bytes, 2, {}, &error), "sender consumes ACK/NACK");
    }
    CHECK(saw_nack, "NACK emitted for missing sequence");
    CHECK(saw_ack, "ACK emitted for received sequences");
    CHECK(sender.pending_outbound() == 1, "NACK schedules exactly missing datagram");
    auto resent = sender.next_outbound();
    CHECK(resent.has_value(), "resent datagram available");
    if (resent) {
        CHECK(resent->sequence == d1->sequence, "resent sequence matches missing datagram");
        sender.mark_sent(*resent, 3);
    }

    // Timer-based resend still operates without an explicit NACK.
    CHECK(sender.queue({0x33}, RakNetReliability::ReliableOrdered, 0, &error), "queue timeout packet");
    auto timeout_packet = sender.next_outbound();
    CHECK(timeout_packet.has_value(), "timeout packet available");
    if (timeout_packet) {
        sender.mark_sent(*timeout_packet, 10);
        sender.tick(31);
        auto timed_retry = sender.next_outbound();
        CHECK(timed_retry.has_value(), "timer schedules retry");
        if (timed_retry) sender.mark_sent(*timed_retry, 31);
    }

    ReliabilityConfig retry_cfg = cfg;
    retry_cfg.max_retries = 1;
    RakNetReliabilityEngine retry_sender(retry_cfg);
    CHECK(retry_sender.queue({0x44}, RakNetReliability::ReliableOrdered, 0, &error), "queue max retry packet");
    auto retry_packet = retry_sender.next_outbound();
    if (retry_packet) {
        retry_sender.mark_sent(*retry_packet, 0);
        retry_sender.tick(20);
        auto retry_once = retry_sender.next_outbound();
        CHECK(retry_once.has_value(), "one retry allowed");
        if (retry_once) retry_sender.mark_sent(*retry_once, 20);
        retry_sender.tick(40);
        CHECK(retry_sender.terminal(), "retry limit becomes terminal");
    }
}

static void test_fragmentation_and_order() {
    ReliabilityConfig cfg;
    cfg.mtu = 120;
    cfg.max_datagram_bytes = 120;
    cfg.max_reassembly_packets = 8;
    cfg.max_reassembly_bytes = 4096;
    std::string error;

    RakNetReliabilityEngine sender(cfg);
    RakNetReliabilityEngine receiver(cfg);
    std::vector<std::uint8_t> large(300);
    for (std::size_t i = 0; i < large.size(); ++i) large[i] = static_cast<std::uint8_t>(i & 0xff);
    CHECK(sender.queue(large, RakNetReliability::ReliableOrdered, 0, &error), "queue fragmented payload");

    std::vector<PendingDatagram> fragments;
    while (auto p = sender.next_outbound()) {
        sender.mark_sent(*p, 10);
        fragments.push_back(std::move(*p));
    }
    CHECK(fragments.size() > 1, "large payload fragmented");
    std::vector<std::vector<std::uint8_t>> delivered;
    for (const auto& p : fragments) {
        CHECK(receiver.ingest(p.bytes, 10, [&](const ReceivedPayload& payload) { delivered.push_back(payload.payload); }, &error), "fragment ingested");
    }
    CHECK(delivered.size() == 1 && delivered[0] == large, "fragmented payload reassembled exactly once");

    ReliabilityConfig order_cfg;
    RakNetReliabilityEngine ordered_sender(order_cfg);
    RakNetReliabilityEngine ordered_receiver(order_cfg);
    CHECK(ordered_sender.queue({0xa0}, RakNetReliability::ReliableOrdered, 0, &error), "queue ordered A");
    CHECK(ordered_sender.queue({0xb0}, RakNetReliability::ReliableOrdered, 0, &error), "queue ordered B");
    auto first = ordered_sender.next_outbound();
    auto second = ordered_sender.next_outbound();
    CHECK(first && second, "ordered pair available");
    if (first && second) {
        std::vector<std::uint8_t> ordered_delivered;
        CHECK(ordered_receiver.ingest(second->bytes, 1, [&](const ReceivedPayload& p) { ordered_delivered.push_back(p.payload[0]); }, &error), "out of order second accepted");
        CHECK(ordered_delivered.empty(), "future order is buffered");
        CHECK(ordered_receiver.ingest(first->bytes, 2, [&](const ReceivedPayload& p) { ordered_delivered.push_back(p.payload[0]); }, &error), "first accepted");
        CHECK(ordered_delivered == std::vector<std::uint8_t>({0xa0, 0xb0}), "ordered messages released in order");
    }

    RakNetReliabilityEngine seq_sender(order_cfg);
    RakNetReliabilityEngine seq_receiver(order_cfg);
    CHECK(seq_sender.queue({0xc0}, RakNetReliability::ReliableSequenced, 0, &error), "queue sequenced old");
    CHECK(seq_sender.queue({0xd0}, RakNetReliability::ReliableSequenced, 0, &error), "queue sequenced new");
    auto old_seq = seq_sender.next_outbound();
    auto new_seq = seq_sender.next_outbound();
    CHECK(old_seq && new_seq, "sequenced pair available");
    if (old_seq && new_seq) {
        std::vector<std::uint8_t> sequenced_delivered;
        CHECK(seq_receiver.ingest(new_seq->bytes, 1, [&](const ReceivedPayload& p) { sequenced_delivered.push_back(p.payload[0]); }, &error), "new sequenced frame accepted");
        CHECK(seq_receiver.ingest(old_seq->bytes, 2, [&](const ReceivedPayload& p) { sequenced_delivered.push_back(p.payload[0]); }, &error), "old sequenced frame ignored safely");
        CHECK(sequenced_delivered == std::vector<std::uint8_t>({0xd0}), "older sequenced payload is suppressed");
    }

}

static void test_full_loopback_raknet_handshake() {
    LoopbackDatagramIo io;
    RakNetConfig cfg;
    cfg.endpoint = {"127.0.0.1", 19132};
    cfg.mtu = 1400;
    cfg.client_guid = 0x1122334455667788ull;

    RakNetTransportBackend backend(cfg);
    backend.set_io(&io);
    ProtocolIdentity id{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    std::string error;
    CHECK(backend.connect(id, &error), "backend begins handshake");
    CHECK(io.outbound().size() == 1 && io.outbound()[0].size() == 1400, "OCR1 is exact negotiated probe size");
    CHECK(io.outbound()[0][17] == 11, "OCR1 protocol version 11");

    io.inject_inbound(make_reply1(0xaabbccddeeff0011ull, 1400));
    CHECK(backend.poll(&error), "Reply1 accepted");
    CHECK(backend.connection().phase() == RakNetPhase::OpenConnection2Sent, "phase OCR2 sent");
    CHECK(io.outbound().back()[0] == offline::OpenConnectionRequest2, "OCR2 sent");
    const auto& ocr2 = io.outbound().back();
    CHECK(ocr2.size() >= 22 && ocr2[17] == 0x04 && ocr2[18] == static_cast<std::uint8_t>(~127u), "OCR2 contains resolved protocol address");

    io.inject_inbound(make_reply2(0xaabbccddeeff0011ull, 1400, 19132));
    CHECK(backend.poll(&error), "Reply2 accepted");
    CHECK(backend.connection().phase() == RakNetPhase::ConnectionRequestSent, "phase connection request sent");

    // The ConnectionRequest is now a reliable ordered frame, not a bare online datagram.
    auto client_wire = take_last(io);
    CHECK(client_wire[0] >= kFrameSetMin && client_wire[0] <= kFrameSetMax, "ConnectionRequest is frame-set encapsulated");

    ReliabilityConfig server_cfg;
    server_cfg.mtu = 1400;
    server_cfg.max_datagram_bytes = 1400;
    RakNetReliabilityEngine server_reliability(server_cfg);
    std::vector<std::vector<std::uint8_t>> server_payloads;
    CHECK(server_reliability.ingest(client_wire, 1, [&](const ReceivedPayload& p) {
        server_payloads.push_back(p.payload);
    }, &error), "server decodes client ConnectionRequest frame");
    CHECK(server_payloads.size() == 1 && server_payloads[0][0] == offline::ConnectionRequest,
          "server receives ConnectionRequest payload");

    server_reliability.tick(1);
    while (auto ack = server_reliability.next_outbound()) io.inject_inbound(std::move(ack->bytes));

    auto accepted_payload = make_connection_accepted(1, 2, 19132);
    CHECK(server_reliability.queue(std::move(accepted_payload), RakNetReliability::ReliableOrdered, 0, &error),
          "server queues ConnectionRequestAccepted reliably");
    auto accepted_frame = server_reliability.next_outbound();
    CHECK(accepted_frame.has_value(), "accepted frame available");
    if (accepted_frame) io.inject_inbound(std::move(accepted_frame->bytes));

    CHECK(backend.poll(&error), "client processes ConnectionRequestAccepted frame");
    CHECK(backend.connection().is_connected(), "client connected");
    CHECK(backend.state() == TransportState::Authenticating, "backend ready for authentication stage");

    // Server ACK for client control frame is not required to enter Connected; it is handled independently.
    CHECK(io.outbound().size() >= 4, "client emitted NewIncomingConnection and ConnectedPing control traffic");
    bool saw_frame = false;
    for (const auto& bytes : io.outbound()) {
        saw_frame = saw_frame || (!bytes.empty() && bytes[0] >= kFrameSetMin && bytes[0] <= kFrameSetMax);
    }
    CHECK(saw_frame, "connected control traffic uses RakNet frame sets");
}


#if !defined(_WIN32)
static bool recv_raw_udp(int fd, std::vector<std::uint8_t>& bytes, sockaddr_storage& from,
                         socklen_t& from_len, int attempts = 100) {
    for (int attempt = 0; attempt < attempts; ++attempt) {
        std::uint8_t buffer[2048];
        from_len = sizeof(from);
        const auto n = ::recvfrom(fd, buffer, sizeof(buffer), 0,
                                  reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n > 0) {
            bytes.assign(buffer, buffer + n);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

static bool send_raw_udp(int fd, const std::vector<std::uint8_t>& bytes,
                         const sockaddr_storage& peer, socklen_t peer_len) {
    const auto sent = ::sendto(fd, bytes.data(), bytes.size(), 0,
                               reinterpret_cast<const sockaddr*>(&peer), peer_len);
    return sent == static_cast<ssize_t>(bytes.size());
}

static void test_live_backend_over_udp() {
    const int server_fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    CHECK(server_fd >= 0, "fake Bedrock UDP server socket created");
    if (server_fd < 0) return;

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(0);
    CHECK(::bind(server_fd, reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr)) == 0,
          "fake Bedrock UDP server bound");
    socklen_t server_len = sizeof(server_addr);
    CHECK(::getsockname(server_fd, reinterpret_cast<sockaddr*>(&server_addr), &server_len) == 0,
          "fake Bedrock UDP server port discovered");
    const int flags = ::fcntl(server_fd, F_GETFL, 0);
    ::fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    SocketDatagramIo socket_io(1400);
    RakNetConfig cfg;
    cfg.endpoint = {"127.0.0.1", ntohs(server_addr.sin_port)};
    cfg.mtu = 1400;
    cfg.max_datagram_bytes = 1400;
    cfg.client_guid = 0x8877665544332211ull;
    cfg.reliability.mtu = 1400;
    cfg.reliability.max_datagram_bytes = 1400;
    RakNetTransportBackend backend(cfg);
    backend.set_io(&socket_io);
    ProtocolIdentity id{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    std::string error;
    CHECK(backend.connect(id, &error), "backend opens real UDP socket and sends OCR1");

    std::vector<std::uint8_t> wire;
    sockaddr_storage client_peer{};
    socklen_t client_peer_len = sizeof(client_peer);
    CHECK(recv_raw_udp(server_fd, wire, client_peer, client_peer_len), "fake server receives OCR1");
    CHECK(!wire.empty() && wire[0] == offline::OpenConnectionRequest1 && wire.size() == 1400,
          "real UDP OCR1 is correctly formatted and MTU-sized");

    CHECK(send_raw_udp(server_fd, make_reply1(0x1020304050607080ull, 1400), client_peer, client_peer_len),
          "fake server sends Reply1");
    CHECK(backend.poll(&error), "backend processes Reply1 over real UDP");

    wire.clear();
    CHECK(recv_raw_udp(server_fd, wire, client_peer, client_peer_len), "fake server receives OCR2");
    CHECK(!wire.empty() && wire[0] == offline::OpenConnectionRequest2, "real UDP OCR2 emitted");

    CHECK(send_raw_udp(server_fd, make_reply2(0x1020304050607080ull, 1400, ntohs(server_addr.sin_port)),
                       client_peer, client_peer_len), "fake server sends Reply2");
    CHECK(backend.poll(&error), "backend processes Reply2 over real UDP");

    wire.clear();
    CHECK(recv_raw_udp(server_fd, wire, client_peer, client_peer_len), "fake server receives framed ConnectionRequest");
    CHECK(!wire.empty() && wire[0] >= kFrameSetMin && wire[0] <= kFrameSetMax,
          "real UDP ConnectionRequest uses frame-set transport");

    ReliabilityConfig server_cfg;
    server_cfg.mtu = 1400;
    server_cfg.max_datagram_bytes = 1400;
    RakNetReliabilityEngine server_reliability(server_cfg);
    std::vector<std::vector<std::uint8_t>> server_payloads;
    CHECK(server_reliability.ingest(wire, 1, [&](const ReceivedPayload& payload) {
        server_payloads.push_back(payload.payload);
    }, &error), "fake server reliability decodes ConnectionRequest");
    CHECK(server_payloads.size() == 1 && server_payloads[0][0] == offline::ConnectionRequest,
          "fake server gets ConnectionRequest payload");
    server_reliability.tick(1);
    while (auto ack = server_reliability.next_outbound()) {
        CHECK(send_raw_udp(server_fd, ack->bytes, client_peer, client_peer_len), "fake server sends ACK");
    }

    auto accepted = make_connection_accepted(1, 2, ntohs(server_addr.sin_port));
    CHECK(server_reliability.queue(std::move(accepted), RakNetReliability::ReliableOrdered, 0, &error),
          "fake server queues reliable ConnectionRequestAccepted");
    auto accepted_frame = server_reliability.next_outbound();
    CHECK(accepted_frame.has_value(), "fake server emits accepted frame");
    if (accepted_frame) {
        CHECK(send_raw_udp(server_fd, accepted_frame->bytes, client_peer, client_peer_len),
              "fake server sends accepted frame");
    }

    CHECK(backend.poll(&error), "backend transitions connected over real UDP");
    CHECK(backend.connection().is_connected(), "real UDP backend is connected");

    // NewIncomingConnection and ConnectedPing are sent through the same live socket/reliability path.
    bool saw_client_connected_packet = false;
    for (int i = 0; i < 3 && !saw_client_connected_packet; ++i) {
        wire.clear();
        if (recv_raw_udp(server_fd, wire, client_peer, client_peer_len, 40)) {
            saw_client_connected_packet = !wire.empty() && wire[0] >= kFrameSetMin && wire[0] <= kFrameSetMax;
        }
    }
    CHECK(saw_client_connected_packet, "real UDP connected control packet observed");
    socket_io.close();
    ::close(server_fd);
}
#else
static void test_live_backend_over_udp() {
    std::cout << "v11 live backend UDP integration: Windows runtime not exercised in this Linux build\n";
}
#endif

#if !defined(_WIN32)
static void test_live_udp_socket() {
    const int server_fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    CHECK(server_fd >= 0, "raw UDP test socket created");
    if (server_fd < 0) return;

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(0);
    CHECK(::bind(server_fd, reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr)) == 0, "raw UDP test socket bound");

    socklen_t server_len = sizeof(server_addr);
    CHECK(::getsockname(server_fd, reinterpret_cast<sockaddr*>(&server_addr), &server_len) == 0, "raw UDP test port discovered");

    const int flags = ::fcntl(server_fd, F_GETFL, 0);
    ::fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    SocketDatagramIo client(512);
    RakNetEndpoint endpoint{"127.0.0.1", ntohs(server_addr.sin_port)};
    std::string error;
    CHECK(client.open(endpoint, &error), "live UDP client socket opens");
    CHECK(client.is_open(), "live UDP client reports open");
    CHECK(client.protocol_peer_address().has_value(), "live UDP peer protocol address available");
    CHECK(client.protocol_local_address().has_value(), "live UDP local protocol address available");

    const std::vector<std::uint8_t> ping{0x12, 0x34, 0x56};
    CHECK(client.send_to(ping, &error), "live UDP sends datagram");

    std::vector<std::uint8_t> received;
    sockaddr_storage client_peer{};
    socklen_t client_peer_len = sizeof(client_peer);
    for (int attempt = 0; attempt < 100 && received.empty(); ++attempt) {
        std::uint8_t buffer[512];
        sockaddr_storage from{};
        socklen_t from_len = sizeof(from);
        const auto n = ::recvfrom(server_fd, buffer, sizeof(buffer), 0,
                                  reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n > 0) {
            received.assign(buffer, buffer + n);
            std::memcpy(&client_peer, &from, static_cast<std::size_t>(from_len));
            client_peer_len = from_len;
        }
        else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(received == ping, "raw UDP server receives client datagram");

    if (!received.empty()) {
        // Echo to the connected client using the same peer address.
        const auto sent = ::sendto(server_fd, received.data(), received.size(), 0,
                                   reinterpret_cast<const sockaddr*>(&client_peer), client_peer_len);
        CHECK(sent == static_cast<ssize_t>(received.size()), "raw UDP server echoes datagram");
    }

    std::optional<std::vector<std::uint8_t>> echoed;
    for (int attempt = 0; attempt < 100 && !echoed.has_value(); ++attempt) {
        echoed = client.recv_from(&error);
        if (!echoed) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(echoed.has_value() && *echoed == ping, "SocketDatagramIo receives echoed datagram");
    client.close();
    CHECK(!client.is_open(), "live UDP client closes");
    ::close(server_fd);
}
#else
static void test_live_udp_socket() {
    std::cout << "v11 socket loopback: Windows runtime not exercised in this Linux build\n";
}
#endif

int main() {
    test_ocr1();
    test_frame_and_ack();
    test_nack_and_resend();
    test_fragmentation_and_order();
    test_full_loopback_raknet_handshake();
    test_live_backend_over_udp();
    test_live_udp_socket();

    if (failures == 0) {
        std::cout << "v11 tests: PASS\n";
        return 0;
    }
    std::cerr << "v11 tests: " << failures << " failure(s)\n";
    return 1;
}
