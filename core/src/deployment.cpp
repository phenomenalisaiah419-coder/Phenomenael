#include "core/deployment.hpp"

namespace bedrockai {

DeploymentDecision DeploymentPlanner::plan(const DeploymentRequest& request) {
    DeploymentDecision out;

    if (!request.same_character) {
        out.detail = "Rejected: deployment must control the existing player character, not a second entity";
        return out;
    }
    if (request.server_install_required) {
        out.detail = "Rejected: server installation is outside the portable client-side design";
        return out;
    }
    if (request.game_version != ProtocolIdentity::kGameVersion || request.protocol != ProtocolIdentity::kProtocol) {
        out.detail = "Rejected: runtime is pinned to Bedrock 1.26.50 / protocol 2193";
        return out;
    }
    if (request.target == WorldTarget::Unknown) {
        out.detail = "Rejected: world target is unknown";
        return out;
    }

    out.portable = true;
    out.accepted = true;

    switch (request.mode) {
    case DeploymentMode::EmbeddedClient:
        out.requires_embedded_client = true;
        out.server_changes_required = false;
        out.detail = "Portable client-side deployment; suitable for local worlds and servers";
        break;
    case DeploymentMode::RelayProxy:
        if (request.target == WorldTarget::LocalWorld) {
            out.detail = "Rejected: a network relay cannot be treated as the local-world integration";
            out.accepted = false;
            out.portable = false;
            break;
        }
        out.requires_embedded_client = false;
        out.server_changes_required = false;
        out.detail = "Portable relay deployment for network-reachable Bedrock targets";
        break;
    case DeploymentMode::CompanionOverlay:
        if (request.target == WorldTarget::LocalWorld) {
            out.detail = "Accepted as experimental companion only; direct local-world control still requires embedded client integration";
        } else {
            out.detail = "Accepted as experimental companion; full autonomous control still uses client/relay integration";
        }
        out.requires_embedded_client = true;
        out.server_changes_required = false;
        break;
    default:
        out.accepted = false;
        out.portable = false;
        out.detail = "Rejected: deployment mode is invalid";
        return out;
    }

    if (!out.accepted) return out;

    switch (request.transport) {
    case TransportPreference::RakNet:
        out.selected_transport = BedrockTransport::RakNet;
        break;
    case TransportPreference::NetherNet:
        out.selected_transport = BedrockTransport::NetherNet;
        break;
    case TransportPreference::Auto:
        // Actual transport negotiation is delegated to the host/backend. Auto
        // means we do not hard-code a transport in the controller core.
        out.selected_transport = BedrockTransport::Unknown;
        break;
    default:
        out.accepted = false;
        out.portable = false;
        out.detail = "Rejected: transport preference is invalid";
        return out;
    }

    return out;
}

PortableControllerRuntime::PortableControllerRuntime(DeploymentRequest request)
    : request_(std::move(request)), session_({
          ProtocolIdentity{ProtocolIdentity::kGameVersion, ProtocolIdentity::kProtocol, BedrockTransport::Unknown},
          128
      }) {}

bool PortableControllerRuntime::configure(std::string* error) {
    decision_ = DeploymentPlanner::plan(request_);
    if (!decision_.accepted) {
        capabilities_ = {};
        if (error) *error = decision_.detail;
        return false;
    }

    capabilities_.can_read_client_state = true;
    capabilities_.can_submit_inputs = true;
    capabilities_.can_intercept_packets = (request_.mode != DeploymentMode::CompanionOverlay);
    capabilities_.can_control_local_world = (request_.mode == DeploymentMode::EmbeddedClient &&
                                              (request_.target == WorldTarget::LocalWorld ||
                                               request_.target == WorldTarget::LANWorld));
    capabilities_.can_control_remote_server = (request_.mode == DeploymentMode::EmbeddedClient ||
                                                request_.mode == DeploymentMode::RelayProxy);
    return true;
}

bool PortableControllerRuntime::attach_transport(BedrockTransportBackend& backend, std::string* error) {
    if (!decision_.accepted) {
        if (error) *error = "runtime has not been configured";
        return false;
    }
    return session_.attach(backend, error);
}

void PortableControllerRuntime::detach_transport() {
    session_.detach();
}

} // namespace bedrockai
