#pragma once

#include "core/bedrock_crypto.hpp"
#include "core/game_batch.hpp"
#include "core/raknet_reliability.hpp"
#include "core/recording.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bedrockai::interop {

// Minimal protocol-2193 reference harness. It is intentionally a test peer, not a
// production server: it exercises the client's actual UDP/RakNet/login/encryption wire path.
class LocalBedrock2193Server {
public:
    LocalBedrock2193Server();
    ~LocalBedrock2193Server();
    LocalBedrock2193Server(const LocalBedrock2193Server&) = delete;
    LocalBedrock2193Server& operator=(const LocalBedrock2193Server&) = delete;

    bool start(std::string* error = nullptr);
    bool poll(std::string* error = nullptr);
    void stop();

    std::uint16_t port() const { return port_; }
    bool saw_request_network_settings() const { return saw_request_network_settings_; }
    bool saw_login() const { return saw_login_; }
    bool saw_client_handshake() const { return saw_client_handshake_; }
    bool sent_server_handshake() const { return sent_server_handshake_; }
    bool sent_start_game() const { return sent_start_game_; }
    bool running() const { return socket_fd_ >= 0; }
    const TransportRecorder& recorder() const { return recorder_; }

private:
    int socket_fd_ = -1;
    std::uint16_t port_ = 0;
    std::uint64_t server_guid_ = 0x6a09e667f3bcc909ULL;
    std::uint64_t client_guid_ = 0;
    std::uint16_t mtu_ = 1492;
    std::uint16_t client_mtu_ = 1492;
    std::vector<std::uint8_t> client_address_;
    std::vector<std::uint8_t> server_address_;
    std::vector<std::uint8_t> local_address_;
    std::size_t client_address_len_ = 0;
    bool got_open1_ = false;
    bool got_open2_ = false;
    bool connection_accepted_ = false;
    bool saw_request_network_settings_ = false;
    bool saw_login_ = false;
    bool saw_client_handshake_ = false;
    bool sent_server_handshake_ = false;
    bool sent_start_game_ = false;
    std::array<std::uint8_t, 32> encryption_key_{};
    BedrockPacketCipher cipher_;
    OpenSSLBedrockCryptoProvider crypto_;
    raknet::RakNetReliabilityEngine reliability_;
    TransportRecorder recorder_{256};
    std::string last_error_;

    bool process_datagram(const std::vector<std::uint8_t>& bytes,
                          const void* peer_addr,
                          std::size_t peer_addr_len,
                          std::string* error);
    bool send_datagram(const std::vector<std::uint8_t>& bytes, std::string* error);
    bool send_offline_reply1(std::string* error);
    bool send_offline_reply2(std::string* error);
    bool send_connection_accepted(std::uint64_t request_timestamp, std::string* error);
    bool handle_connected_payload(const raknet::ReceivedPayload& payload, std::string* error);
    bool send_game_packet(std::uint32_t id, const std::vector<std::uint8_t>& payload, bool encrypted, std::string* error);
    bool handle_login_payload(const std::vector<std::uint8_t>& payload, std::string* error);
    bool handle_client_game_batch(const std::vector<std::uint8_t>& wire, std::string* error);
};

} // namespace bedrockai::interop
