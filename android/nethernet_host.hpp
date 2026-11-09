#pragma once

#include "core/nethernet_transport.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace bedrockai::android_host {

class NetherNetHostHandle {
public:
    NetherNetHostHandle();
    ~NetherNetHostHandle() = default;
    NetherNetHostHandle(const NetherNetHostHandle&) = delete;
    NetherNetHostHandle& operator=(const NetherNetHostHandle&) = delete;

    bool connect(std::string* error = nullptr);
    bool receive(const std::vector<std::uint8_t>& data, std::string* error = nullptr);
    bool poll(std::string* error = nullptr);
    std::vector<std::uint8_t> next_outbound();
    std::size_t next_outbound_size() const;
    bool connected() const { return backend_.state() == TransportState::Authenticating || backend_.state() == TransportState::InGame; }

private:
    NetherNetDataChannelIo io_;
    NetherNetTransportBackend backend_;
    mutable std::mutex outbound_mu_;
    std::deque<std::vector<std::uint8_t>> outbound_;
};

} // namespace bedrockai::android_host

extern "C" {
std::uintptr_t bedrockai_nethernet_create();
void bedrockai_nethernet_destroy(std::uintptr_t handle);
int bedrockai_nethernet_connect(std::uintptr_t handle);
int bedrockai_nethernet_receive(std::uintptr_t handle, const std::uint8_t* data, std::size_t len);
int bedrockai_nethernet_poll(std::uintptr_t handle);
std::size_t bedrockai_nethernet_next_outbound_size(std::uintptr_t handle);
std::size_t bedrockai_nethernet_next_outbound(std::uintptr_t handle, std::uint8_t* out, std::size_t max_len);
}
