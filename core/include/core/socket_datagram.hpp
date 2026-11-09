#pragma once

#include "raknet_transport.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#endif

namespace bedrockai {
namespace raknet {

// Real non-blocking UDP implementation for Android/NDK, Linux and desktop Windows/POSIX.
// The socket is connected to one remote UDP peer, so recv() is filtered to that peer.
class SocketDatagramIo : public DatagramIo {
public:
    explicit SocketDatagramIo(std::size_t max_datagram_bytes = 2048);
    ~SocketDatagramIo() override;

    bool open(const RakNetEndpoint& endpoint, std::string* error) override;
    bool send_to(const std::vector<std::uint8_t>& bytes, std::string* error) override;
    std::optional<std::vector<std::uint8_t>> recv_from(std::string* error) override;
    void close() override;
    bool is_open() const override;

    std::optional<std::vector<std::uint8_t>> protocol_peer_address() const override;
    std::optional<std::vector<std::uint8_t>> protocol_local_address() const override;

private:
#if defined(_WIN32)
    using NativeSocket = SOCKET;
    static constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
    using NativeSocket = int;
    static constexpr NativeSocket kInvalidSocket = -1;
#endif

    std::size_t max_datagram_bytes_ = 2048;
    bool open_ = false;
    NativeSocket socket_fd_ = kInvalidSocket;
    std::vector<std::uint8_t> peer_protocol_address_;
    std::vector<std::uint8_t> local_protocol_address_;
#if defined(_WIN32)
    bool wsa_started_ = false;
#endif

    bool set_nonblocking(std::string* error);
    bool resolve_and_connect(const RakNetEndpoint& endpoint, std::string* error);
    void close_handle();
};

} // namespace raknet
} // namespace bedrockai
