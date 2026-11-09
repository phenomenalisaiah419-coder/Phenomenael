#include "core/login_payload.hpp"
#include "core/secure_runtime.hpp"
#include "core/socket_datagram.hpp"
#include "core/raknet_transport.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using namespace bedrockai;

namespace {
std::string env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

bool read_text_file(const std::string& path, std::string& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { error = "unable to open " + path; return false; }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (!file.good() && !file.eof()) { error = "unable to read " + path; return false; }
    return true;
}

bool split_endpoint(const std::string& endpoint, std::string& host, std::uint16_t& port) {
    const std::size_t pos = endpoint.rfind(':');
    if (pos == std::string::npos || pos == 0 || pos + 1 >= endpoint.size()) return false;
    host = endpoint.substr(0, pos);
    const long parsed = std::strtol(endpoint.substr(pos + 1).c_str(), nullptr, 10);
    if (parsed <= 0 || parsed > 65535) return false;
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

class FileCredentialProvider final : public BedrockLoginAuthProvider {
public:
    bool get_credentials(const LoginAuthRequestContext& context,
                         LoginCredentialMaterial& out,
                         std::string* error) override {
        if (!context.protocol.is_target()) { if (error) *error = "unsupported protocol"; return false; }
        if (!read_text_file(chain_path_, out.login_chain_json, *error)) return false;
        if (!token_path_.empty() && !read_text_file(token_path_, out.multiplayer_token, *error)) return false;
        if (!read_text_file(client_data_path_, out.client_data_json, *error)) return false;
        out.authentication_type = auth_type_;
        if (out.authentication_type == 0 && out.multiplayer_token.empty()) {
            if (error) *error = "modern authentication requires BEDROCKAI_MULTIPLAYER_TOKEN_FILE";
            return false;
        }
        return true;
    }

    std::string chain_path_;
    std::string token_path_;
    std::string client_data_path_;
    std::uint8_t auth_type_ = 0;
};
}

int main() {
    const std::string endpoint = env_or_empty("BEDROCKAI_SERVER");
    const std::string chain_file = env_or_empty("BEDROCKAI_LOGIN_CHAIN_FILE");
    const std::string client_file = env_or_empty("BEDROCKAI_CLIENT_DATA_FILE");
    const std::string token_file = env_or_empty("BEDROCKAI_MULTIPLAYER_TOKEN_FILE");
    const std::string auth_type_text = env_or_empty("BEDROCKAI_AUTH_TYPE");

    if (endpoint.empty() || chain_file.empty() || client_file.empty()) {
        std::cerr << "live_compat_runner: required environment not configured\n";
        return 2;
    }

    std::string host;
    std::uint16_t port = 0;
    if (!split_endpoint(endpoint, host, port)) {
        std::cerr << "live_compat_runner: invalid BEDROCKAI_SERVER\n";
        return 2;
    }

    FileCredentialProvider auth;
    auth.chain_path_ = chain_file;
    auth.token_path_ = token_file;
    auth.client_data_path_ = client_file;
    if (!auth_type_text.empty()) {
        const long parsed = std::strtol(auth_type_text.c_str(), nullptr, 10);
        if (parsed != 0 && parsed != 2) {
            std::cerr << "live_compat_runner: BEDROCKAI_AUTH_TYPE must be 0 or 2\n";
            return 2;
        }
        auth.auth_type_ = static_cast<std::uint8_t>(parsed);
    }

    raknet::SocketDatagramIo io(2048);
    raknet::RakNetConfig raknet_config;
    raknet_config.endpoint = {host, port};
    raknet_config.mtu = 1492;
    raknet_config.max_datagram_bytes = 2048;
    raknet_config.identity = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    raknet::RakNetTransportBackend backend(raknet_config);
    backend.set_io(&io);

    OpenSSLBedrockCryptoProvider crypto;
    SecureRuntimeConfig config;
    config.network.expected = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    config.bootstrap.expected = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    Bedrock2193SecureRuntime runtime(crypto, config);
    std::string error;
    if (!runtime.attach(backend, &error)) {
        std::cerr << "live_compat_runner: attach failed: " << error << '\n';
        return 1;
    }
    LoginAuthRequestContext context;
    context.protocol = {ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::RakNet};
    context.server_address = endpoint;
    if (!runtime.start_login(auth, context, &error)) {
        std::cerr << "live_compat_runner: login setup failed: " << error << '\n';
        return 1;
    }

    constexpr int kIterations = 6000; // bounded live probe window; host decides the actual timeout.
    for (int i = 0; i < kIterations; ++i) {
        std::string poll_error;
        if (!backend.poll(&poll_error)) {
            std::cerr << "live_compat_runner: transport error: " << poll_error << '\n';
            return 1;
        }
        if (!runtime.pump(&poll_error)) {
            std::cerr << "live_compat_runner: runtime error: " << poll_error << '\n';
            return 1;
        }
        if (runtime.phase() == LoginPhase::WorldReady) {
            std::cout << "live_compat_runner: WorldReady\n";
            runtime.detach();
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cerr << "live_compat_runner: timeout before WorldReady\n";
    runtime.detach();
    return 1;
}
