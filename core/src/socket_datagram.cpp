#include "core/socket_datagram.hpp"

#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace bedrockai {
namespace raknet {
namespace {

void set_error(std::string* error, const std::string& value) {
    if (error) *error = value;
}

std::string system_error(const char* prefix) {
#if defined(_WIN32)
    return std::string(prefix) + ": WSA error " + std::to_string(WSAGetLastError());
#else
    return std::string(prefix) + ": " + std::strerror(errno);
#endif
}

void put_be16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void put_le16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void put_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
}

bool encode_protocol_address(const sockaddr* sa, std::vector<std::uint8_t>& out) {
    if (!sa) return false;
    out.clear();
    if (sa->sa_family == AF_INET) {
        const auto* addr = reinterpret_cast<const sockaddr_in*>(sa);
        const auto ip = ntohl(addr->sin_addr.s_addr);
        out.push_back(0x04);
        out.push_back(static_cast<std::uint8_t>(~((ip >> 24) & 0xffu)));
        out.push_back(static_cast<std::uint8_t>(~((ip >> 16) & 0xffu)));
        out.push_back(static_cast<std::uint8_t>(~((ip >> 8) & 0xffu)));
        out.push_back(static_cast<std::uint8_t>(~(ip & 0xffu)));
        put_be16(out, ntohs(addr->sin_port));
        return true;
    }
    if (sa->sa_family == AF_INET6) {
        const auto* addr = reinterpret_cast<const sockaddr_in6*>(sa);
        out.push_back(0x06);
        put_le16(out, static_cast<std::uint16_t>(AF_INET6));
        put_be16(out, ntohs(addr->sin6_port));
        put_be32(out, addr->sin6_flowinfo);
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&addr->sin6_addr);
        out.insert(out.end(), raw, raw + 16);
        put_be32(out, addr->sin6_scope_id);
        return true;
    }
    return false;
}



} // namespace

SocketDatagramIo::SocketDatagramIo(std::size_t max_datagram_bytes)
    : max_datagram_bytes_(max_datagram_bytes) {
#if defined(_WIN32)
    WSADATA data{};
    wsa_started_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
}

SocketDatagramIo::~SocketDatagramIo() {
    close();
#if defined(_WIN32)
    if (wsa_started_) WSACleanup();
#endif
}

bool SocketDatagramIo::resolve_and_connect(const RakNetEndpoint& endpoint, std::string* error) {
    if (endpoint.host.empty() || endpoint.port == 0) {
        set_error(error, "UDP endpoint host/port is invalid");
        return false;
    }
#if defined(_WIN32)
    if (!wsa_started_) {
        set_error(error, "Winsock initialization failed");
        return false;
    }
#endif

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* results = nullptr;
    const auto port = std::to_string(endpoint.port);
    const int rc = getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints, &results);
    if (rc != 0) {
#if defined(_WIN32)
        set_error(error, std::string("getaddrinfo failed: ") + gai_strerrorA(rc));
#else
        set_error(error, std::string("getaddrinfo failed: ") + gai_strerror(rc));
#endif
        return false;
    }

    bool connected = false;
    for (addrinfo* it = results; it != nullptr; it = it->ai_next) {
        auto fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
#if defined(_WIN32)
        if (fd == INVALID_SOCKET) continue;
        const int connect_rc = ::connect(fd, it->ai_addr, static_cast<int>(it->ai_addrlen));
#else
        if (fd < 0) continue;
        const int connect_rc = ::connect(fd, it->ai_addr, it->ai_addrlen);
#endif
        if (connect_rc != 0) {
#if defined(_WIN32)
            closesocket(fd);
#else
            ::close(fd);
#endif
            continue;
        }

        if (!encode_protocol_address(it->ai_addr, peer_protocol_address_)) {
#if defined(_WIN32)
            closesocket(fd);
#else
            ::close(fd);
#endif
            peer_protocol_address_.clear();
            continue;
        }

        sockaddr_storage local{};
#if defined(_WIN32)
        int local_len = static_cast<int>(sizeof(local));
        const int getsock_rc = ::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &local_len);
#else
        socklen_t local_len = static_cast<socklen_t>(sizeof(local));
        const int getsock_rc = ::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &local_len);
#endif
        if (getsock_rc != 0 || !encode_protocol_address(reinterpret_cast<const sockaddr*>(&local), local_protocol_address_)) {
#if defined(_WIN32)
            closesocket(fd);
#else
            ::close(fd);
#endif
            peer_protocol_address_.clear();
            local_protocol_address_.clear();
            continue;
        }

        socket_fd_ = fd;
        connected = true;
        break;
    }
    freeaddrinfo(results);

    if (!connected) {
        set_error(error, "unable to create/connect UDP socket");
        return false;
    }
    return true;
}

bool SocketDatagramIo::set_nonblocking(std::string* error) {
#if defined(_WIN32)
    u_long mode = 1;
    if (ioctlsocket(socket_fd_, FIONBIO, &mode) != 0) {
        set_error(error, system_error("ioctlsocket(FIONBIO) failed"));
        return false;
    }
#else
    const int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        set_error(error, system_error("fcntl(O_NONBLOCK) failed"));
        return false;
    }
#endif
    return true;
}

bool SocketDatagramIo::open(const RakNetEndpoint& endpoint, std::string* error) {
    if (open_) {
        set_error(error, "UDP socket already open");
        return false;
    }
    if (max_datagram_bytes_ == 0) {
        set_error(error, "UDP maximum datagram size is zero");
        return false;
    }
    peer_protocol_address_.clear();
    local_protocol_address_.clear();
    if (!resolve_and_connect(endpoint, error)) return false;
    if (!set_nonblocking(error)) {
        close_handle();
        return false;
    }
    open_ = true;
    return true;
}

bool SocketDatagramIo::send_to(const std::vector<std::uint8_t>& bytes, std::string* error) {
    if (!open_) {
        set_error(error, "UDP socket not open");
        return false;
    }
    if (bytes.empty() || bytes.size() > max_datagram_bytes_) {
        set_error(error, "UDP datagram size exceeds configured limit");
        return false;
    }
#if defined(_WIN32)
    const int sent = ::send(socket_fd_, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0);
    if (sent != static_cast<int>(bytes.size())) {
        set_error(error, system_error("send failed"));
        return false;
    }
#else
    const auto sent = ::send(socket_fd_, bytes.data(), bytes.size(), 0);
    if (sent != static_cast<ssize_t>(bytes.size())) {
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            set_error(error, "UDP send would block");
        } else {
            set_error(error, system_error("send failed"));
        }
        return false;
    }
#endif
    return true;
}

std::optional<std::vector<std::uint8_t>> SocketDatagramIo::recv_from(std::string* error) {
    if (!open_) return std::nullopt;
    std::vector<std::uint8_t> bytes(max_datagram_bytes_);
#if defined(_WIN32)
    const int received = ::recv(socket_fd_, reinterpret_cast<char*>(bytes.data()), static_cast<int>(bytes.size()), 0);
    if (received < 0) {
        const int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK || e == WSAETIMEDOUT) return std::nullopt;
        set_error(error, system_error("recv failed"));
        return std::nullopt;
    }
#else
    const auto received = ::recv(socket_fd_, bytes.data(), bytes.size(), 0);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return std::nullopt;
        set_error(error, system_error("recv failed"));
        return std::nullopt;
    }
#endif
    bytes.resize(static_cast<std::size_t>(received));
    if (bytes.empty()) return std::nullopt;
    return bytes;
}

void SocketDatagramIo::close_handle() {
#if defined(_WIN32)
    if (socket_fd_ != kInvalidSocket) closesocket(socket_fd_);
#else
    if (socket_fd_ != kInvalidSocket) ::close(socket_fd_);
#endif
    socket_fd_ = kInvalidSocket;
    open_ = false;
    peer_protocol_address_.clear();
    local_protocol_address_.clear();
}

void SocketDatagramIo::close() {
    close_handle();
}

bool SocketDatagramIo::is_open() const {
    return open_;
}

std::optional<std::vector<std::uint8_t>> SocketDatagramIo::protocol_peer_address() const {
    if (peer_protocol_address_.empty()) return std::nullopt;
    return peer_protocol_address_;
}

std::optional<std::vector<std::uint8_t>> SocketDatagramIo::protocol_local_address() const {
    if (local_protocol_address_.empty()) return std::nullopt;
    return local_protocol_address_;
}

} // namespace raknet
} // namespace bedrockai
