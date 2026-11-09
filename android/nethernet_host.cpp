#include "nethernet_host.hpp"

#include <algorithm>
#include <new>

namespace bedrockai::android_host {

NetherNetHostHandle::NetherNetHostHandle()
    : backend_(io_) {
    io_.set_send_handler([this](const std::vector<std::uint8_t>& data, std::string*) {
        std::lock_guard<std::mutex> lock(outbound_mu_);
        if (outbound_.size() >= 4096) return false;
        outbound_.push_back(data);
        return true;
    });
}

bool NetherNetHostHandle::connect(std::string* error) {
    ProtocolIdentity identity{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::NetherNet};
    return backend_.connect(identity, error);
}

bool NetherNetHostHandle::receive(const std::vector<std::uint8_t>& data, std::string* error) {
    if (data.empty()) {
        if (error) *error = "NetherNet host received an empty data-channel message";
        return false;
    }
    io_.receive(data);
    return true;
}

bool NetherNetHostHandle::poll(std::string* error) {
    return backend_.poll(error);
}

std::size_t NetherNetHostHandle::next_outbound_size() const {
    std::lock_guard<std::mutex> lock(outbound_mu_);
    return outbound_.empty() ? 0u : outbound_.front().size();
}

std::vector<std::uint8_t> NetherNetHostHandle::next_outbound() {
    std::lock_guard<std::mutex> lock(outbound_mu_);
    if (outbound_.empty()) return {};
    auto data = std::move(outbound_.front());
    outbound_.pop_front();
    return data;
}

} // namespace bedrockai::android_host

extern "C" {
std::uintptr_t bedrockai_nethernet_create() {
    auto* handle = new (std::nothrow) bedrockai::android_host::NetherNetHostHandle();
    return reinterpret_cast<std::uintptr_t>(handle);
}
void bedrockai_nethernet_destroy(std::uintptr_t handle) {
    delete reinterpret_cast<bedrockai::android_host::NetherNetHostHandle*>(handle);
}
int bedrockai_nethernet_connect(std::uintptr_t handle) {
    auto* h = reinterpret_cast<bedrockai::android_host::NetherNetHostHandle*>(handle);
    if (!h) return 0;
    return h->connect() ? 1 : 0;
}
int bedrockai_nethernet_receive(std::uintptr_t handle, const std::uint8_t* data, std::size_t len) {
    auto* h = reinterpret_cast<bedrockai::android_host::NetherNetHostHandle*>(handle);
    if (!h || !data || len == 0) return 0;
    return h->receive(std::vector<std::uint8_t>(data, data + len)) ? 1 : 0;
}
int bedrockai_nethernet_poll(std::uintptr_t handle) {
    auto* h = reinterpret_cast<bedrockai::android_host::NetherNetHostHandle*>(handle);
    if (!h) return 0;
    return h->poll() ? 1 : 0;
}
std::size_t bedrockai_nethernet_next_outbound_size(std::uintptr_t handle) {
    auto* h = reinterpret_cast<bedrockai::android_host::NetherNetHostHandle*>(handle);
    if (!h) return 0;
    return h->next_outbound_size();
}
std::size_t bedrockai_nethernet_next_outbound(std::uintptr_t handle, std::uint8_t* out, std::size_t max_len) {
    auto* h = reinterpret_cast<bedrockai::android_host::NetherNetHostHandle*>(handle);
    if (!h || !out || max_len == 0) return 0;
    const auto data = h->next_outbound();
    if (data.empty()) return 0;
    const std::size_t copied = std::min(max_len, data.size());
    std::copy_n(data.begin(), copied, out);
    return copied;
}
}
