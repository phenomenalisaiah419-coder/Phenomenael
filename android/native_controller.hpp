#pragma once

#include "android_jca_crypto_provider.hpp"
#include "core/deployment.hpp"
#include "core/network_recovery.hpp"
#include "core/raknet_transport.hpp"
#include "core/socket_datagram.hpp"
#include "core/replay_backend.hpp"
#include "core/auth_cache.hpp"
#include "core/secure_runtime.hpp"
#include "core/bedrock2193_observation_decoder.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace bedrockai::android_host {

class NativeControllerHandle {
public:
    NativeControllerHandle();
    ~NativeControllerHandle();
    NativeControllerHandle(const NativeControllerHandle&) = delete;
    NativeControllerHandle& operator=(const NativeControllerHandle&) = delete;

    bool configure(WorldTarget target, TransportPreference transport, bool embedded_client,
                   std::string* error = nullptr);
    bool connect_raknet(const std::string& host, std::uint16_t port, std::string* error = nullptr);
    bool connect_secure_raknet(const std::string& host, std::uint16_t port,
                               const std::string& cache_directory, BedrockCryptoProvider& crypto,
                               std::string* error = nullptr);
    bool poll(std::string* error = nullptr);
    void network_lost();
    bool network_available(std::uint64_t generation, std::string* error = nullptr);

    bool reconnect_required() const;
    std::uint64_t network_generation() const;
    TransportState transport_state() const;
    bool can_control_local() const;
    bool can_control_remote() const;
    std::string last_error() const;
    bool read_gameplay_observation(GameplayObservation& observation, std::string* error = nullptr);
    Bedrock2193ObservationDecoder& observation_decoder() { return observation_decoder_; }
    const Bedrock2193ObservationDecoder& observation_decoder() const { return observation_decoder_; }

    PortableControllerRuntime& runtime() { return runtime_; }
    const PortableControllerRuntime& runtime() const { return runtime_; }

private:
    PortableControllerRuntime runtime_;
    std::unique_ptr<raknet::SocketDatagramIo> socket_io_;
    std::unique_ptr<raknet::RakNetTransportBackend> raknet_backend_;
    std::unique_ptr<Bedrock2193SecureRuntime> secure_runtime_;
    std::unique_ptr<FileLoginCacheProvider> login_cache_;
    Bedrock2193ObservationDecoder observation_decoder_{};
    BedrockCryptoProvider* secure_crypto_ = nullptr; // borrowed; host must keep provider alive
    std::string login_cache_directory_;
    std::string last_error_;
    bool secure_mode_ = false;
    std::string endpoint_host_;
    std::uint16_t endpoint_port_ = 0;
    TransportPreference configured_transport_ = TransportPreference::Auto;
    TransportRecoveryCoordinator recovery_{};

    mutable std::recursive_mutex mutex_;

    bool reconnect_raknet(std::string* error);
    bool connect_raknet_impl(const std::string& host, std::uint16_t port, bool reset_recovery, std::string* error);
    bool connect_secure_raknet_impl(const std::string& host, std::uint16_t port,
                                    const std::string& cache_directory, BedrockCryptoProvider& crypto,
                                    bool reset_recovery, std::string* error);
    void clear_secure_session();
};

} // namespace bedrockai::android_host

extern "C" {
std::uintptr_t bedrockai_native_create();
void bedrockai_native_destroy(std::uintptr_t handle);
int bedrockai_native_configure(std::uintptr_t handle, int target, int transport, int embedded_client);
int bedrockai_native_local_control_capable(std::uintptr_t handle);
int bedrockai_native_remote_control_capable(std::uintptr_t handle);
int bedrockai_native_connect_raknet(std::uintptr_t handle, const char* host, std::uint16_t port);
int bedrockai_native_connect_secure_raknet(std::uintptr_t handle, const char* host, std::uint16_t port,
                                           const char* cache_directory, std::uintptr_t crypto_provider);
int bedrockai_native_poll(std::uintptr_t handle);
const char* bedrockai_native_last_error(std::uintptr_t handle);
void bedrockai_native_network_lost(std::uintptr_t handle);
int bedrockai_native_network_available(std::uintptr_t handle, std::uint64_t generation);
int bedrockai_native_reconnect_required(std::uintptr_t handle);
std::uint64_t bedrockai_native_network_generation(std::uintptr_t handle);
}
