#include "core/raknet_transport.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace bedrockai;
using namespace bedrockai::raknet;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++failures; } } while (0)

static std::uint64_t monotonic_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

static void put_be16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

static void put_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

static void put_be64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xffU));
}

static std::vector<std::uint8_t> reply1(std::uint64_t guid, std::uint16_t mtu,
                                        bool has_security, std::uint32_t cookie = 0U) {
    std::vector<std::uint8_t> out{offline::OpenConnectionReply1};
    const auto& magic = offline_magic();
    out.insert(out.end(), magic.begin(), magic.end());
    put_be64(out, guid);
    out.push_back(has_security ? 1U : 0U);
    if (has_security) put_be32(out, cookie);
    put_be16(out, mtu);
    return out;
}

static std::vector<std::uint8_t> reply2(std::uint64_t guid, std::uint16_t mtu, std::uint16_t port,
                                        bool encryption_enabled = false) {
    std::vector<std::uint8_t> out{offline::OpenConnectionReply2};
    const auto& magic = offline_magic();
    out.insert(out.end(), magic.begin(), magic.end());
    put_be64(out, guid);
    out.insert(out.end(), {0x04U, static_cast<std::uint8_t>(~127U), static_cast<std::uint8_t>(~0U),
                           static_cast<std::uint8_t>(~0U), static_cast<std::uint8_t>(~1U)});
    put_be16(out, port);
    put_be16(out, mtu);
    out.push_back(encryption_enabled ? 1U : 0U);
    return out;
}

static void test_reply1_security_cookie_round_trip() {
    LoopbackDatagramIo io;
    RakNetConfig config;
    config.endpoint = {"127.0.0.1", 19132};
    config.client_guid = 0x1122334455667788ULL;
    config.handshake_retry_interval_ms = 500U;
    RakNetConnection connection(config);
    connection.set_io(&io);
    std::string error;
    CHECK(connection.start(&error), "cookie test connection starts");

    constexpr std::uint32_t cookie = 0xA1B2C3D4U;
    CHECK(connection.on_datagram(reply1(0x0102030405060708ULL, 1400U, true, cookie), &error),
          "Reply1 with security cookie is accepted");
    CHECK(connection.phase() == RakNetPhase::OpenConnection2Sent, "cookie Reply1 advances to Request2");
    CHECK(io.outbound().size() >= 2U, "Request2 emitted after cookie Reply1");
    if (io.outbound().size() >= 2U) {
        const auto& request2 = io.outbound().back();
        CHECK(request2[0] == offline::OpenConnectionRequest2, "security-cookie request uses Request2 ID");
        CHECK(request2.size() > 22U, "security-cookie Request2 includes cookie, flag and address");
        if (request2.size() > 22U) {
            CHECK(request2[17] == 0xA1U && request2[18] == 0xB2U &&
                  request2[19] == 0xC3U && request2[20] == 0xD4U, "cookie is copied in big-endian order");
            CHECK(request2[21] == 0U, "Bedrock client security-support flag remains false");
        }
    }
    CHECK(connection.on_datagram(reply2(0x0102030405060708ULL, 1400U, 19132U), &error),
          "Reply2 advances cookie-enabled server handshake");
    CHECK(connection.phase() == RakNetPhase::ConnectionRequestSent,
          "cookie-enabled server reaches reliable ConnectionRequest stage");
}

static void test_vanilla_bedrock_keeps_legacy_raknet_encryption_off() {
    const auto request = OfflineMessageCodec::connection_request(0x1122334455667788ULL, 1234U);
    CHECK(request.size() == 18U, "ConnectionRequest has the vanilla Bedrock profile size");
    CHECK(!request.empty() && request.back() == 0U,
          "ConnectionRequest legacy security flag remains false for vanilla Bedrock compatibility");

    LoopbackDatagramIo io;
    RakNetConfig config;
    config.endpoint = {"127.0.0.1", 19132};
    config.client_guid = 9U;
    RakNetConnection connection(config);
    connection.set_io(&io);
    std::string error;
    CHECK(connection.start(&error), "legacy encryption compatibility connection starts");
    CHECK(connection.on_datagram(reply1(11U, 1400U, false), &error), "ordinary Reply1 is accepted");
    CHECK(!connection.on_datagram(reply2(11U, 1400U, 19132U, true), &error),
          "Reply2 requesting legacy RakNet encryption is rejected rather than mis-negotiated");
    CHECK(error.find("encryption-enabled server is outside the Bedrock profile") != std::string::npos,
          "legacy RakNet encryption rejection is explicit");
}

static void test_poll_frequency_does_not_cause_timeout() {
    LoopbackDatagramIo io;
    RakNetConfig config;
    config.endpoint = {"127.0.0.1", 19132};
    config.client_guid = 5U;
    RakNetTransportBackend backend(config);
    backend.set_io(&io);
    const ProtocolIdentity identity{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol,
                                    BedrockTransport::RakNet};
    std::string error;
    CHECK(backend.connect(identity, &error), "timeout regression connection starts");
    for (int i = 0; i < 1000; ++i) {
        CHECK(backend.poll(&error), "rapid poll calls do not consume the wall-clock handshake timeout");
        if (!error.empty()) break;
    }
    CHECK(backend.connection().phase() == RakNetPhase::OpenConnection1Sent,
          "rapid UI/game-frame polling does not terminate the RakNet handshake");
    CHECK(error.empty(), "rapid polling has no timeout error");
}

static void test_handshake_timeout_is_wall_clock_based_and_stage_specific() {
    LoopbackDatagramIo io;
    RakNetConfig config;
    config.endpoint = {"127.0.0.1", 19132};
    config.client_guid = 6U;
    config.handshake_timeout_ms = 1000U;
    RakNetConnection connection(config);
    connection.set_io(&io);
    std::string error;
    CHECK(connection.start(&error), "wall-clock timeout connection starts");
    const auto future = monotonic_ms() + config.handshake_timeout_ms + 20U;
    CHECK(!connection.tick_at(future, &error), "elapsed handshake deadline terminates stalled connection");
    CHECK(connection.phase() == RakNetPhase::Failed, "elapsed timeout marks connection failed");
    CHECK(error.find("OpenConnectionReply1") != std::string::npos,
          "timeout reports the exact RakNet stage instead of a generic timeout");
}

static void test_backend_preserves_stage_error_without_output_buffer() {
    LoopbackDatagramIo io;
    RakNetConfig config;
    config.endpoint = {"127.0.0.1", 19132};
    config.client_guid = 8U;
    config.handshake_timeout_ms = 1000U;
    RakNetTransportBackend backend(config);
    backend.set_io(&io);
    const ProtocolIdentity identity{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol,
                                    BedrockTransport::RakNet};
    std::string error;
    CHECK(backend.connect(identity, &error), "diagnostic regression connection starts");
    const auto future = monotonic_ms() + config.handshake_timeout_ms + 20U;
    CHECK(!backend.connection().tick_at(future, nullptr), "timed-out connection fails without an error output buffer");
    CHECK(backend.last_error().find("waiting for OpenConnectionReply1") != std::string::npos,
          "backend exposes stage-specific timeout when Android JNI passes no error buffer");
}

static void test_mtu_probe_reduces_after_retry_interval() {
    LoopbackDatagramIo io;
    RakNetConfig config;
    config.endpoint = {"127.0.0.1", 19132};
    config.client_guid = 7U;
    config.mtu = 1492U;
    config.minimum_probe_mtu = 576U;
    config.mtu_probe_step = 100U;
    config.handshake_retry_interval_ms = 40U;
    RakNetConnection connection(config);
    connection.set_io(&io);
    std::string error;
    CHECK(connection.start(&error), "MTU probe connection starts");
    CHECK(connection.tick_at(monotonic_ms() + 100U, &error), "timed retry sends a smaller MTU probe");
    CHECK(io.outbound().size() == 2U, "retry emitted one new Request1 probe");
    if (io.outbound().size() == 2U) {
        CHECK(io.outbound().back().size() == 1392U, "retry reduces MTU padding by configured step");
    }
}

int main() {
    test_reply1_security_cookie_round_trip();
    test_vanilla_bedrock_keeps_legacy_raknet_encryption_off();
    test_poll_frequency_does_not_cause_timeout();
    test_handshake_timeout_is_wall_clock_based_and_stage_specific();
    test_backend_preserves_stage_error_without_output_buffer();
    test_mtu_probe_reduces_after_retry_interval();
    if (failures == 0) std::cout << "All v27 RakNet security/timeout tests passed\n";
    return failures == 0 ? 0 : 1;
}
