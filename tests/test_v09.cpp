#include "core/session_bootstrap.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace bedrockai;

struct MockBackend : BedrockTransportBackend {
    TransportState s = TransportState::Disconnected;
    ReceiveHandler h;
    std::vector<TransportFrame> sent;

    bool connect(const ProtocolIdentity& id, std::string* e) override {
        if (!id.is_target()) {
            if (e) *e = "bad protocol";
            return false;
        }
        s = TransportState::Authenticating;
        return true;
    }
    bool send(const TransportFrame& f, std::string*) override {
        sent.push_back(f);
        return true;
    }
    void close() override { s = TransportState::Closing; }
    TransportState state() const override { return s; }
    void set_receive_handler(ReceiveHandler x) override { h = std::move(x); }
};

static TransportFrame make_ns(const std::vector<std::uint8_t>& payload) {
    TransportFrame ns;
    ns.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    ns.direction = Direction::Clientbound;
    ns.packet_id = packet_id::NetworkSettings;
    ns.sequence = 0;
    ns.payload = payload;
    return ns;
}

static TransportFrame make_disconnect() {
    TransportFrame f;
    f.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    f.direction = Direction::Clientbound;
    f.packet_id = packet_id::Disconnect;
    f.sequence = 0;
    return f;
}

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << "\n"; \
        ++failures; \
    } \
} while (0)

int main() {
    // --- Happy path ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach");
        Bedrock2193LoginBootstrap boot;
        CHECK(boot.start(session, &e), "start");
        CHECK(b.sent.size() == 1, "one RequestNetworkSettings sent");
        CHECK(b.sent[0].packet_id == packet_id::RequestNetworkSettings, "packet id is RequestNetworkSettings");
        CHECK(b.sent[0].payload.size() == 4, "body is 4-byte BE protocol");
        // 2193 = 0x00000891 big-endian
        CHECK(b.sent[0].payload[0] == 0x00 && b.sent[0].payload[1] == 0x00 &&
              b.sent[0].payload[2] == 0x08 && b.sent[0].payload[3] == 0x91, "BE protocol 2193");

        auto ns = make_ns({100, 0, 0xff, 0xff, 0, 50, 0, 0, 0, 0});
        CHECK(boot.handle_clientbound(ns, &e), "NetworkSettings accepted");
        CHECK(!boot.compression_enabled(), "compression disabled (0xffff)");
        CHECK(boot.phase() == LoginPhase::NetworkSettingsReceived, "phase NetworkSettingsReceived");
        CHECK(boot.network_settings().compression_threshold == 100, "threshold 100");
        CHECK(boot.network_settings().client_throttle_threshold == 50, "throttle threshold");

        CHECK(boot.provide_login_payload(session, {1, 2, 3}, &e), "login payload");
        CHECK(boot.phase() == LoginPhase::EncryptionNegotiating, "phase EncryptionNegotiating");
        CHECK(boot.mark_encryption_established(&e), "encryption established");
        CHECK(boot.mark_world_ready(&e), "world ready");
        CHECK(boot.phase() == LoginPhase::WorldReady, "phase WorldReady");
        CHECK(boot.is_terminal(), "terminal after WorldReady");
    }

    // --- Compression enabled (algorithm 0) ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach2");
        Bedrock2193LoginBootstrap boot;
        CHECK(boot.start(session, &e), "start2");
        // threshold=256, algorithm=0 (zlib), throttle=1, thr=10, scalar=0.5f (approx LE)
        std::vector<std::uint8_t> payload = {0x00, 0x01, 0x00, 0x00, 1, 10, 0, 0, 0, 0x3f};
        CHECK(boot.handle_clientbound(make_ns(payload), &e), "ns with compression");
        CHECK(boot.compression_enabled(), "compression enabled");
        CHECK(boot.network_settings().compression_algorithm == 0, "algorithm 0");
    }

    // --- Trailing bytes tolerated ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach3");
        Bedrock2193LoginBootstrap boot;
        CHECK(boot.start(session, &e), "start3");
        std::vector<std::uint8_t> payload = {100, 0, 0xff, 0xff, 0, 50, 0, 0, 0, 0, 0xde, 0xad};
        CHECK(boot.handle_clientbound(make_ns(payload), &e), "trailing bytes ok");
        CHECK(boot.phase() == LoginPhase::NetworkSettingsReceived, "phase after trailing");
    }

    // --- Negative: short NetworkSettings ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach4");
        Bedrock2193LoginBootstrap boot;
        CHECK(boot.start(session, &e), "start4");
        auto ns = make_ns({1, 2, 3}); // too short
        CHECK(!boot.handle_clientbound(ns, &e), "short payload rejected");
        CHECK(boot.phase() == LoginPhase::Failed, "phase Failed");
    }

    // --- Negative: unsupported compression algorithm ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach5");
        Bedrock2193LoginBootstrap boot;
        CHECK(boot.start(session, &e), "start5");
        // algorithm = 5 (unsupported)
        auto ns = make_ns({100, 0, 5, 0, 0, 50, 0, 0, 0, 0});
        CHECK(!boot.handle_clientbound(ns, &e), "bad algorithm rejected");
        CHECK(boot.phase() == LoginPhase::Failed, "phase Failed after bad algo");
    }

    // --- Negative: NetworkSettings in wrong phase ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach6");
        Bedrock2193LoginBootstrap boot;
        // do not start; still Idle
        auto ns = make_ns({100, 0, 0xff, 0xff, 0, 50, 0, 0, 0, 0});
        CHECK(!boot.handle_clientbound(ns, &e), "NS before start rejected");
        CHECK(boot.phase() == LoginPhase::Failed, "Failed on unexpected NS");
    }

    // --- Negative: empty / oversized login payload ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach7");
        Bedrock2193LoginBootstrap boot;
        CHECK(boot.start(session, &e), "start7");
        CHECK(boot.handle_clientbound(make_ns({100, 0, 0xff, 0xff, 0, 50, 0, 0, 0, 0}), &e), "ns7");
        CHECK(!boot.provide_login_payload(session, {}, &e), "empty login rejected");
        CHECK(boot.phase() == LoginPhase::Failed, "Failed on empty login");
    }
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach8");
        LoginBootstrapConfig cfg;
        cfg.max_login_payload = 8;
        Bedrock2193LoginBootstrap boot(cfg);
        CHECK(boot.start(session, &e), "start8");
        CHECK(boot.handle_clientbound(make_ns({100, 0, 0xff, 0xff, 0, 50, 0, 0, 0, 0}), &e), "ns8");
        std::vector<std::uint8_t> big(16, 0xab);
        CHECK(!boot.provide_login_payload(session, big, &e), "oversized login rejected");
        CHECK(boot.phase() == LoginPhase::Failed, "Failed on oversized login");
    }

    // --- Negative: login before NetworkSettings ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach9");
        Bedrock2193LoginBootstrap boot;
        CHECK(boot.start(session, &e), "start9");
        CHECK(!boot.provide_login_payload(session, {1, 2, 3}, &e), "login before NS rejected");
        CHECK(boot.phase() == LoginPhase::Failed, "Failed");
    }

    // --- Disconnect mid-bootstrap ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach10");
        Bedrock2193LoginBootstrap boot;
        CHECK(boot.start(session, &e), "start10");
        CHECK(!boot.handle_clientbound(make_disconnect(), &e), "disconnect returns false");
        CHECK(boot.phase() == LoginPhase::Closed, "phase Closed");
    }

    // --- mark_* in wrong phase ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach11");
        Bedrock2193LoginBootstrap boot;
        CHECK(!boot.mark_encryption_established(&e), "encrypt before login rejected");
        CHECK(!boot.mark_world_ready(&e), "world ready before auth rejected");
    }

    // --- require_encryption = false skips encryption phase ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach12");
        LoginBootstrapConfig cfg;
        cfg.require_encryption = false;
        Bedrock2193LoginBootstrap boot(cfg);
        CHECK(boot.start(session, &e), "start12");
        CHECK(boot.handle_clientbound(make_ns({100, 0, 0xff, 0xff, 0, 50, 0, 0, 0, 0}), &e), "ns12");
        CHECK(boot.provide_login_payload(session, {9, 9, 9}, &e), "login12");
        CHECK(boot.phase() == LoginPhase::Authenticated, "directly Authenticated");
        CHECK(boot.mark_world_ready(&e), "world ready12");
        CHECK(boot.phase() == LoginPhase::WorldReady, "WorldReady12");
    }

    // --- Double start rejected ---
    {
        MockBackend b;
        Protocol2193Session session;
        std::string e;
        CHECK(session.attach(b, &e), "attach13");
        Bedrock2193LoginBootstrap boot;
        CHECK(boot.start(session, &e), "start13");
        CHECK(!boot.start(session, &e), "double start rejected");
        CHECK(boot.phase() == LoginPhase::Failed, "Failed after double start");
    }

    if (failures == 0) {
        std::cout << "v09 tests: PASS\n";
        return 0;
    }
    std::cerr << "v09 tests: " << failures << " failure(s)\n";
    return 1;
}
