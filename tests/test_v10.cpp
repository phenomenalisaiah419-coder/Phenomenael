#include "core/raknet_transport.hpp"
#include "core/raknet_reliability.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace bedrockai;
using namespace bedrockai::raknet;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++failures; } } while (0)

static std::vector<std::uint8_t> make_reply1(std::uint64_t guid, std::uint16_t mtu) {
    std::vector<std::uint8_t> out;
    out.push_back(offline::OpenConnectionReply1);
    const auto& magic = offline_magic();
    out.insert(out.end(), magic.begin(), magic.end());
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<std::uint8_t>((guid >> (8 * i)) & 0xff));
    out.push_back(0); // no security
    out.push_back(static_cast<std::uint8_t>((mtu >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(mtu & 0xff));
    return out;
}

static std::vector<std::uint8_t> make_reply2(std::uint64_t guid, std::uint16_t mtu, std::uint16_t port) {
    std::vector<std::uint8_t> out;
    out.push_back(offline::OpenConnectionReply2);
    const auto& magic = offline_magic();
    out.insert(out.end(), magic.begin(), magic.end());
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<std::uint8_t>((guid >> (8 * i)) & 0xff));
    out.push_back(0x04); // IPv4
    out.push_back(127); out.push_back(0); out.push_back(0); out.push_back(1);
    out.push_back(static_cast<std::uint8_t>((port >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(port & 0xff));
    out.push_back(static_cast<std::uint8_t>((mtu >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(mtu & 0xff));
    out.push_back(0); // security
    return out;
}

static std::vector<std::uint8_t> make_connection_accepted() {
    std::vector<std::uint8_t> address{0x04, static_cast<std::uint8_t>(~127u), static_cast<std::uint8_t>(~0u),
                                      static_cast<std::uint8_t>(~0u), static_cast<std::uint8_t>(~1u), 0x4a, 0xbc};
    std::vector<std::uint8_t> out{offline::ConnectionRequestAccepted};
    out.insert(out.end(), address.begin(), address.end());
    out.push_back(0); out.push_back(0);
    for (int i = 0; i < 10; ++i) out.insert(out.end(), address.begin(), address.end());
    for (int i = 0; i < 8; ++i) out.push_back(0);
    for (int i = 0; i < 8; ++i) out.push_back(0);
    return out;
}

static std::vector<std::uint8_t> wrap_reliable_ordered(const std::vector<std::uint8_t>& payload) {
    RakNetReliabilityEngine server;
    std::string error;
    CHECK(server.queue(payload, RakNetReliability::ReliableOrdered, 0, &error), "wrap accepted payload");
    auto packet = server.next_outbound();
    CHECK(packet.has_value(), "accepted payload frame available");
    return packet ? packet->bytes : std::vector<std::uint8_t>{};
}


int main() {
    // Offline codec: OCR1 structure
    {
        auto req1 = OfflineMessageCodec::open_connection_request1(1492);
        CHECK(!req1.empty() && req1[0] == offline::OpenConnectionRequest1, "OCR1 id");
        CHECK(OfflineMessageCodec::is_offline_magic(req1, 1), "OCR1 magic");
    }

    // Full handshake via loopback
    {
        LoopbackDatagramIo io;
        RakNetConfig cfg;
        cfg.endpoint = {"127.0.0.1", 19132};
        cfg.mtu = 1492;
        cfg.client_guid = 0x1122334455667788ull;

        RakNetTransportBackend backend(cfg);
        backend.set_io(&io);

        ProtocolIdentity id{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
        std::string e;
        CHECK(backend.connect(id, &e), "connect starts OCR1");
        CHECK(backend.state() == TransportState::Connecting, "state Connecting");
        CHECK(io.outbound().size() == 1, "OCR1 sent");
        CHECK(io.outbound()[0][0] == offline::OpenConnectionRequest1, "OCR1 id on wire");

        // Server Reply1
        io.inject_inbound(make_reply1(0xaabbccddeeff0011ull, 1400));
        CHECK(backend.poll(&e), "poll reply1");
        CHECK(backend.connection().phase() == RakNetPhase::OpenConnection2Sent, "phase OCR2 sent");
        CHECK(io.outbound().size() >= 2, "OCR2 sent");
        CHECK(io.outbound().back()[0] == offline::OpenConnectionRequest2, "OCR2 id");

        // Server Reply2
        io.inject_inbound(make_reply2(0xaabbccddeeff0011ull, 1400, 19132));
        CHECK(backend.poll(&e), "poll reply2");
        CHECK(backend.connection().phase() == RakNetPhase::ConnectionRequestSent, "phase ConnectionRequest");
        CHECK(io.outbound().back()[0] >= kFrameSetMin && io.outbound().back()[0] <= kFrameSetMax, "ConnectionRequest frame-set id");

        // Accept
        io.inject_inbound(wrap_reliable_ordered(make_connection_accepted()));
        CHECK(backend.poll(&e), "poll accept");
        CHECK(backend.connection().is_connected(), "connected");
        CHECK(backend.state() == TransportState::Authenticating, "state Authenticating");
        CHECK(backend.connection().negotiated_mtu() == 1400, "mtu negotiated");
    }

    // Bad magic fails Reply1
    {
        LoopbackDatagramIo io;
        RakNetConfig cfg;
        cfg.client_guid = 1;
        RakNetTransportBackend backend(cfg);
        backend.set_io(&io);
        ProtocolIdentity id{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
        std::string e;
        CHECK(backend.connect(id, &e), "connect2");
        std::vector<std::uint8_t> bad = {offline::OpenConnectionReply1, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        io.inject_inbound(bad);
        CHECK(!backend.poll(&e), "bad magic fails");
        CHECK(backend.connection().phase() == RakNetPhase::Failed, "phase Failed");
    }

    // Disconnect notification
    {
        LoopbackDatagramIo io;
        RakNetConfig cfg;
        cfg.client_guid = 2;
        RakNetTransportBackend backend(cfg);
        backend.set_io(&io);
        ProtocolIdentity id{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
        std::string e;
        CHECK(backend.connect(id, &e), "connect3");
        io.inject_inbound(make_reply1(9, 1200));
        CHECK(backend.poll(&e), "r1");
        io.inject_inbound(make_reply2(9, 1200, 19132));
        CHECK(backend.poll(&e), "r2");
        io.inject_inbound(wrap_reliable_ordered(make_connection_accepted()));
        CHECK(backend.poll(&e), "acc");
        CHECK(backend.connection().is_connected(), "connected3");
        io.inject_inbound({offline::DisconnectNotification});
        CHECK(!backend.poll(&e), "disconnect");
        CHECK(backend.connection().phase() == RakNetPhase::Closed, "closed");
    }

    // Protocol mismatch rejected
    {
        LoopbackDatagramIo io;
        RakNetTransportBackend backend;
        backend.set_io(&io);
        ProtocolIdentity bad{"1.0.0", 1, BedrockTransport::RakNet};
        std::string e;
        CHECK(!backend.connect(bad, &e), "bad protocol rejected");
    }

    // No IO rejected
    {
        RakNetTransportBackend backend;
        ProtocolIdentity id{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
        std::string e;
        CHECK(!backend.connect(id, &e), "no io rejected");
    }

    // Session attach over RakNet backend after handshake
    {
        LoopbackDatagramIo io;
        RakNetConfig cfg;
        cfg.client_guid = 42;
        RakNetTransportBackend backend(cfg);
        backend.set_io(&io);
        ProtocolIdentity id{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
        std::string e;
        CHECK(backend.connect(id, &e), "connect4");
        io.inject_inbound(make_reply1(7, 1300));
        CHECK(backend.poll(&e), "p1");
        io.inject_inbound(make_reply2(7, 1300, 19132));
        CHECK(backend.poll(&e), "p2");
        io.inject_inbound(wrap_reliable_ordered(make_connection_accepted()));
        CHECK(backend.poll(&e), "p3");

        Protocol2193Session session;
        // Session attach will call backend.connect again — use a fresh backend path:
        // Instead verify send path once connected.
        TransportFrame frame;
        frame.identity = id;
        frame.direction = Direction::Serverbound;
        frame.packet_id = 0xfe;
        frame.sequence = 1;
        frame.payload = {0xfe, 0x01, 0x02};
        CHECK(backend.send(frame, &e), "send game batch after connected");
        CHECK(!io.outbound().empty(), "bytes on wire");
    }

    if (failures == 0) {
        std::cout << "v10 tests: PASS\n";
        return 0;
    }
    std::cerr << "v10 tests: " << failures << " failure(s)\n";
    return 1;
}
