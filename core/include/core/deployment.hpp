#pragma once

#include "protocol_adapter.hpp"
#include "transport.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace bedrockai {

enum class DeploymentMode {
    EmbeddedClient,   // Controller lives inside the Bedrock client process/APK.
    RelayProxy,       // Client connects through a portable network relay.
    CompanionOverlay  // Experimental Android companion; perception/input aid only.
};

enum class WorldTarget {
    LocalWorld,
    LANWorld,
    Realm,
    RemoteServer,
    Unknown
};

enum class TransportPreference {
    Auto,
    RakNet,
    NetherNet
};

struct DeploymentRequest {
    DeploymentMode mode = DeploymentMode::EmbeddedClient;
    WorldTarget target = WorldTarget::Unknown;
    TransportPreference transport = TransportPreference::Auto;
    bool same_character = true;
    bool server_install_required = false;
    std::string game_version = ProtocolIdentity::kGameVersion;
    std::uint32_t protocol = ProtocolIdentity::kProtocol;
};

struct DeploymentDecision {
    bool accepted = false;
    bool portable = false;
    bool requires_embedded_client = false;
    bool server_changes_required = false;
    BedrockTransport selected_transport = BedrockTransport::Unknown;
    std::string detail;
};

class DeploymentPlanner {
public:
    static DeploymentDecision plan(const DeploymentRequest& request);
};

struct RuntimeCapabilities {
    bool can_read_client_state = false;
    bool can_submit_inputs = false;
    bool can_intercept_packets = false;
    bool can_control_local_world = false;
    bool can_control_remote_server = false;
};

class PortableControllerRuntime {
public:
    explicit PortableControllerRuntime(DeploymentRequest request = {});

    bool configure(std::string* error = nullptr);
    bool attach_transport(BedrockTransportBackend& backend, std::string* error = nullptr);
    void detach_transport();

    const DeploymentDecision& decision() const { return decision_; }
    const RuntimeCapabilities& capabilities() const { return capabilities_; }
    Protocol2193Session& session() { return session_; }
    const Protocol2193Session& session() const { return session_; }

private:
    DeploymentRequest request_{};
    DeploymentDecision decision_{};
    RuntimeCapabilities capabilities_{};
    Protocol2193Session session_{};
};

} // namespace bedrockai
