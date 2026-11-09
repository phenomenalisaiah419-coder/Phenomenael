#include "native_controller.hpp"

#include <new>
#include <string>
#include <utility>

namespace bedrockai::android_host {

NativeControllerHandle::NativeControllerHandle()
    : runtime_(DeploymentRequest{}),
      recovery_(3) {
    recovery_.set_reconnect_handler([this](std::string* error) { return reconnect_raknet(error); });
}

NativeControllerHandle::~NativeControllerHandle() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    clear_secure_session();
    observation_decoder_.reset();
    runtime_.detach_transport();
    raknet_backend_.reset();
    socket_io_.reset();
}

void NativeControllerHandle::clear_secure_session() {
    if (secure_runtime_) secure_runtime_->detach();
    secure_runtime_.reset();
    login_cache_.reset();
    secure_crypto_ = nullptr;
    login_cache_directory_.clear();
    secure_mode_ = false;
}

bool NativeControllerHandle::configure(WorldTarget target, TransportPreference transport, bool embedded_client,
                                       std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    DeploymentRequest request;
    request.mode = embedded_client ? DeploymentMode::EmbeddedClient : DeploymentMode::RelayProxy;
    request.target = target;
    request.transport = transport;
    request.same_character = true;
    request.server_install_required = false;
    configured_transport_ = transport;
    runtime_ = PortableControllerRuntime(request);
    observation_decoder_.reset();
    recovery_.reset();
    clear_secure_session();
    socket_io_.reset();
    raknet_backend_.reset();
    endpoint_host_.clear();
    endpoint_port_ = 0;
    last_error_.clear();
    return runtime_.configure(error);
}

bool NativeControllerHandle::connect_raknet(const std::string& host, std::uint16_t port, std::string* error) {
    return connect_raknet_impl(host, port, true, error);
}

bool NativeControllerHandle::connect_raknet_impl(const std::string& host, std::uint16_t port, bool reset_recovery,
                                                  std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto set_error = [this, error](const std::string& message) {
        last_error_ = message;
        if (error) *error = message;
    };
    if (configured_transport_ == TransportPreference::NetherNet) {
        set_error("runtime is configured for NetherNet, not RakNet");
        return false;
    }
    if (!runtime_.decision().accepted) {
        set_error("runtime has not been configured");
        return false;
    }
    if (host.empty() || port == 0) {
        set_error("RakNet host/port is invalid");
        return false;
    }
    clear_secure_session();
    runtime_.detach_transport();
    raknet_backend_.reset();
    socket_io_ = std::make_unique<raknet::SocketDatagramIo>(2048);
    raknet::RakNetConfig config;
    config.endpoint = {host, port};
    config.mtu = 1492;
    config.max_datagram_bytes = 2048;
    config.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    raknet_backend_ = std::make_unique<raknet::RakNetTransportBackend>(config);
    raknet_backend_->set_io(socket_io_.get());
    endpoint_host_ = host;
    endpoint_port_ = port;
    if (reset_recovery) recovery_.reset();
    if (!runtime_.attach_transport(*raknet_backend_, error)) {
        set_error(error && !error->empty() ? *error : "failed to attach RakNet transport");
        raknet_backend_.reset(); socket_io_.reset();
        return false;
    }
    last_error_.clear();
    return true;
}

bool NativeControllerHandle::connect_secure_raknet(const std::string& host, std::uint16_t port,
                                                   const std::string& cache_directory,
                                                   BedrockCryptoProvider& crypto, std::string* error) {
    return connect_secure_raknet_impl(host, port, cache_directory, crypto, true, error);
}

bool NativeControllerHandle::connect_secure_raknet_impl(const std::string& host, std::uint16_t port,
                                                        const std::string& cache_directory,
                                                        BedrockCryptoProvider& crypto, bool reset_recovery,
                                                        std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string local_error;
    std::string* const reported_error = error ? error : &local_error;
    const auto fail_with = [this, reported_error](const std::string& message) {
        last_error_ = message;
        *reported_error = message;
        return false;
    };

    if (configured_transport_ == TransportPreference::NetherNet) {
        return fail_with("runtime is configured for NetherNet, not RakNet");
    }
    if (!runtime_.decision().accepted) return fail_with("runtime has not been configured");
    if (host.empty() || port == 0U) return fail_with("RakNet host/port is invalid");
    if (cache_directory.empty()) return fail_with("secure RakNet requires an app-private login cache directory");

    clear_secure_session();
    runtime_.detach_transport();
    raknet_backend_.reset();
    socket_io_.reset();

    auto login_cache = std::make_unique<FileLoginCacheProvider>(crypto, cache_directory);
    std::string cache_host = host;
    if (cache_host.find(':') != std::string::npos && cache_host.front() != '[') {
        cache_host = "[" + cache_host + "]";
    }
    LoginAuthRequestContext context;
    context.protocol = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    context.server_address = cache_host + ":" + std::to_string(port);

    // Reject the wrong/missing target-bound cache before opening the socket.
    // The secure runtime then loads the same artifact again when NetworkSettings
    // arrives, ensuring this P-384 identity key is paired with the actual Login.
    LoginArtifact cache_probe;
    if (!login_cache->get_prebuilt_login(context, cache_probe, reported_error)) {
        last_error_ = reported_error->empty() ? "login cache validation failed" : *reported_error;
        return false;
    }

    socket_io_ = std::make_unique<raknet::SocketDatagramIo>(2048);
    raknet::RakNetConfig transport_config;
    transport_config.endpoint = {host, port};
    transport_config.mtu = 1492;
    transport_config.max_datagram_bytes = 2048;
    transport_config.identity = context.protocol;
    transport_config.support_server_security_cookie = true;
    raknet_backend_ = std::make_unique<raknet::RakNetTransportBackend>(transport_config);
    raknet_backend_->set_io(socket_io_.get());

    SecureRuntimeConfig secure_config;
    secure_config.network.expected = context.protocol;
    secure_config.bootstrap.expected = context.protocol;
    secure_runtime_ = std::make_unique<Bedrock2193SecureRuntime>(crypto, secure_config);
    if (!secure_runtime_->attach(*raknet_backend_, reported_error)) {
        last_error_ = reported_error->empty() ? "secure runtime could not attach to RakNet" : *reported_error;
        clear_secure_session();
        raknet_backend_.reset();
        socket_io_.reset();
        return false;
    }
    observation_decoder_.reset();
    secure_runtime_->set_receive_handler([this](const TransportFrame& frame) {
        ObservationDecodeReport report;
        std::string decode_error;
        (void)observation_decoder_.accept_frame(frame, &report, &decode_error);
        // Unsupported packets are explicitly reported by the decoder and do not poison the
        // encrypted session. A malformed supported packet remains visible via decoder.last_error().
    });
    login_cache_ = std::move(login_cache);
    if (!secure_runtime_->start_login(*login_cache_, context, reported_error)) {
        last_error_ = reported_error->empty() ? "secure login bootstrap could not start" : *reported_error;
        clear_secure_session();
        raknet_backend_.reset();
        socket_io_.reset();
        return false;
    }

    secure_crypto_ = &crypto;
    login_cache_directory_ = cache_directory;
    secure_mode_ = true;
    endpoint_host_ = host;
    endpoint_port_ = port;
    if (reset_recovery) recovery_.reset();
    last_error_.clear();
    return true;
}

bool NativeControllerHandle::poll(std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (error) error->clear();
    if (!raknet_backend_) {
        last_error_ = "no RakNet backend is connected";
        if (error) *error = last_error_;
        return false;
    }
    const bool ok = raknet_backend_->poll(error);
    if (!ok) {
        // The JNI API commonly calls poll() without an error output pointer.
        // Preserve the actual handshake stage / reliability reason rather than
        // replacing it with a generic message that cannot diagnose a timeout.
        if (error && !error->empty()) {
            last_error_ = *error;
        } else if (secure_runtime_ && !secure_runtime_->last_error().empty()) {
            last_error_ = secure_runtime_->last_error();
        } else if (!raknet_backend_->last_error().empty()) {
            last_error_ = raknet_backend_->last_error();
        } else {
            last_error_ = "RakNet poll failed without a transport diagnostic";
        }
        if (error) *error = last_error_;
        return false;
    }
    if (secure_runtime_) {
        if (!secure_runtime_->pump(error)) {
            last_error_ = error && !error->empty() ? *error : secure_runtime_->last_error();
            return false;
        }
        if (!secure_runtime_->last_error().empty()) {
            last_error_ = secure_runtime_->last_error();
            if (error) *error = last_error_;
            return false;
        }
    }
    last_error_.clear();
    return true;
}

void NativeControllerHandle::network_lost() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    recovery_.on_network_lost();
    observation_decoder_.reset();
    if (secure_runtime_) secure_runtime_->detach();
    runtime_.detach_transport();
    raknet_backend_.reset();
    socket_io_.reset();
}

bool NativeControllerHandle::reconnect_raknet(std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (endpoint_host_.empty() || endpoint_port_ == 0) {
        if (error) *error = "no stored RakNet endpoint available for network recovery";
        return false;
    }
    // Use copies because reconnecting intentionally tears down the old cache/runtime
    // objects before building a fresh connection attempt.
    const std::string host = endpoint_host_;
    const std::uint16_t port = endpoint_port_;
    if (secure_mode_ && secure_crypto_ && !login_cache_directory_.empty()) {
        const std::string cache_directory = login_cache_directory_;
        BedrockCryptoProvider* crypto = secure_crypto_;
        return connect_secure_raknet_impl(host, port, cache_directory, *crypto, false, error);
    }
    return connect_raknet_impl(host, port, false, error);
}

bool NativeControllerHandle::network_available(std::uint64_t generation, std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return recovery_.on_network_available_with_hint(generation, error);
}

} // namespace bedrockai::android_host

extern "C" {

std::uintptr_t bedrockai_native_create() {
    auto* handle = new (std::nothrow) bedrockai::android_host::NativeControllerHandle();
    return reinterpret_cast<std::uintptr_t>(handle);
}

void bedrockai_native_destroy(std::uintptr_t handle) {
    delete reinterpret_cast<bedrockai::android_host::NativeControllerHandle*>(handle);
}

int bedrockai_native_configure(std::uintptr_t handle, int target, int transport, int embedded_client) {
    auto* h = reinterpret_cast<bedrockai::android_host::NativeControllerHandle*>(handle);
    if (!h) return 0;
    const auto t = static_cast<bedrockai::WorldTarget>(target);
    const auto p = static_cast<bedrockai::TransportPreference>(transport);
    return h->configure(t, p, embedded_client != 0) ? 1 : 0;
}

int bedrockai_native_local_control_capable(std::uintptr_t handle) {
    const auto* h = reinterpret_cast<const bedrockai::android_host::NativeControllerHandle*>(handle);
    return h && h->can_control_local() ? 1 : 0;
}

int bedrockai_native_remote_control_capable(std::uintptr_t handle) {
    const auto* h = reinterpret_cast<const bedrockai::android_host::NativeControllerHandle*>(handle);
    return h && h->can_control_remote() ? 1 : 0;
}

int bedrockai_native_connect_raknet(std::uintptr_t handle, const char* host, std::uint16_t port) {
    auto* h = reinterpret_cast<bedrockai::android_host::NativeControllerHandle*>(handle);
    if (!h || host == nullptr) return 0;
    return h->connect_raknet(host, port) ? 1 : 0;
}

int bedrockai_native_connect_secure_raknet(std::uintptr_t handle, const char* host, std::uint16_t port,
                                           const char* cache_directory, std::uintptr_t crypto_provider) {
    auto* h = reinterpret_cast<bedrockai::android_host::NativeControllerHandle*>(handle);
    auto* crypto = reinterpret_cast<bedrockai::BedrockCryptoProvider*>(crypto_provider);
    if (!h || !host || !cache_directory || !crypto) return 0;
    return h->connect_secure_raknet(host, port, cache_directory, *crypto) ? 1 : 0;
}

int bedrockai_native_poll(std::uintptr_t handle) {
    auto* h = reinterpret_cast<bedrockai::android_host::NativeControllerHandle*>(handle);
    if (!h) return 0;
    return h->poll() ? 1 : 0;
}

const char* bedrockai_native_last_error(std::uintptr_t handle) {
    auto* h = reinterpret_cast<bedrockai::android_host::NativeControllerHandle*>(handle);
    if (!h) return "invalid native controller handle";
    thread_local std::string snapshot;
    snapshot = h->last_error();
    return snapshot.c_str();
}

void bedrockai_native_network_lost(std::uintptr_t handle) {
    auto* h = reinterpret_cast<bedrockai::android_host::NativeControllerHandle*>(handle);
    if (h) h->network_lost();
}

int bedrockai_native_network_available(std::uintptr_t handle, std::uint64_t generation) {
    auto* h = reinterpret_cast<bedrockai::android_host::NativeControllerHandle*>(handle);
    if (!h) return 0;
    return h->network_available(generation) ? 1 : 0;
}

int bedrockai_native_reconnect_required(std::uintptr_t handle) {
    const auto* h = reinterpret_cast<const bedrockai::android_host::NativeControllerHandle*>(handle);
    return h && h->reconnect_required() ? 1 : 0;
}

std::uint64_t bedrockai_native_network_generation(std::uintptr_t handle) {
    const auto* h = reinterpret_cast<const bedrockai::android_host::NativeControllerHandle*>(handle);
    return h ? h->network_generation() : 0;
}

}

namespace bedrockai::android_host {

bool NativeControllerHandle::reconnect_required() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return recovery_.recovery_required();
}

std::uint64_t NativeControllerHandle::network_generation() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return recovery_.generation();
}

TransportState NativeControllerHandle::transport_state() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (secure_runtime_) return secure_runtime_->network().state();
    return runtime_.session().state();
}

bool NativeControllerHandle::can_control_local() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return runtime_.capabilities().can_control_local_world;
}

bool NativeControllerHandle::can_control_remote() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return runtime_.capabilities().can_control_remote_server;
}

bool NativeControllerHandle::read_gameplay_observation(GameplayObservation& observation, std::string* error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!secure_mode_ || !secure_runtime_ || secure_runtime_->network().state() != TransportState::InGame) {
        if (error) *error = "secure Bedrock world is not ready; stale observations are not exposed";
        return false;
    }
    if (!observation_decoder_.read(observation, error)) return false;
    return true;
}

std::string NativeControllerHandle::last_error() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!last_error_.empty()) return last_error_;
    if (secure_runtime_ && !secure_runtime_->last_error().empty()) return secure_runtime_->last_error();
    if (raknet_backend_ && !raknet_backend_->last_error().empty()) return raknet_backend_->last_error();
    return {};
}

} // namespace bedrockai::android_host
