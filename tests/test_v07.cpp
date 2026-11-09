#include "core/deployment.hpp"
#include <cassert>
#include <iostream>

using namespace bedrockai;

class FakeBackend final : public BedrockTransportBackend {
public:
    bool connect(const ProtocolIdentity& identity, std::string* error) override {
        if (!identity.is_target()) { if (error) *error = "bad identity"; return false; }
        state_ = TransportState::Authenticating; return true;
    }
    bool send(const TransportFrame&, std::string*) override { return true; }
    void close() override { state_ = TransportState::Disconnected; }
    TransportState state() const override { return state_; }
    void set_receive_handler(ReceiveHandler handler) override { handler_ = std::move(handler); }
private:
    TransportState state_ = TransportState::Disconnected;
    ReceiveHandler handler_;
};

static void test_embedded_local_world() {
    DeploymentRequest r;
    r.mode = DeploymentMode::EmbeddedClient;
    r.target = WorldTarget::LocalWorld;
    auto d = DeploymentPlanner::plan(r);
    assert(d.accepted && d.portable);
    assert(d.requires_embedded_client);
    assert(d.server_changes_required == false);
}

static void test_relay_remote_server() {
    DeploymentRequest r;
    r.mode = DeploymentMode::RelayProxy;
    r.target = WorldTarget::RemoteServer;
    auto d = DeploymentPlanner::plan(r);
    assert(d.accepted && d.portable);
    assert(!d.requires_embedded_client);
}

static void test_relay_local_world_rejected() {
    DeploymentRequest r;
    r.mode = DeploymentMode::RelayProxy;
    r.target = WorldTarget::LocalWorld;
    auto d = DeploymentPlanner::plan(r);
    assert(!d.accepted);
}

static void test_server_install_rejected() {
    DeploymentRequest r;
    r.target = WorldTarget::RemoteServer;
    r.server_install_required = true;
    auto d = DeploymentPlanner::plan(r);
    assert(!d.accepted);
}

static void test_wrong_version_rejected() {
    DeploymentRequest r;
    r.game_version = "1.26.40";
    auto d = DeploymentPlanner::plan(r);
    assert(!d.accepted);
}

static void test_runtime_configuration() {
    DeploymentRequest r;
    r.mode = DeploymentMode::EmbeddedClient;
    r.target = WorldTarget::LocalWorld;
    PortableControllerRuntime runtime(r);
    std::string error;
    assert(runtime.configure(&error));
    assert(runtime.capabilities().can_control_local_world);
    assert(runtime.capabilities().can_submit_inputs);

    FakeBackend backend;
    assert(runtime.attach_transport(backend, &error));
    runtime.detach_transport();
}

int main() {
    test_embedded_local_world();
    test_relay_remote_server();
    test_relay_local_world_rejected();
    test_server_install_rejected();
    test_wrong_version_rejected();
    test_runtime_configuration();
    std::cout << "v0.7 deployment tests: PASS\n";
}
