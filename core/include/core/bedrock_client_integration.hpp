#pragma once

#include "aarch64_signature.hpp"
#include "hook_binding.hpp"
#include "arm64_inline_hook.hpp"
#include "embedded_client_bridge.hpp"
#include "mechanics_probe.hpp"
#include "client_native_abi.hpp"
#include "client_hook_event.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace bedrockai {

class ClientHookDispatcher;
inline constexpr std::uint32_t kEmbeddedClientAbiVersion = 2;
inline constexpr std::uint32_t kEmbeddedClientProtocol = 2193;
inline constexpr const char* kEmbeddedClientGameVersion = "1.26.50";
inline constexpr const char* kEmbeddedClientLibrary = "libminecraftpe.so";

struct ClientEntityView {
    std::uint64_t runtime_id = 0;
    int type_id = 0;
    Vec3 position{};
    Vec3 velocity{};
    double health = 0;
    bool hostile = false;
    bool alive = true;
};

struct ClientBlockView {
    int x = 0;
    int y = 0;
    int z = 0;
    int runtime_id = 0;
    bool solid = false;
    bool replaceable = false;
};

struct ClientStateView {
    std::uint64_t tick = 0;
    int dimension = 0;
    PlayerState player{};
    ServerCorrection last_correction{};
    bool joined = false;
    const ClientEntityView* entities = nullptr;
    std::size_t entity_count = 0;
    const ClientBlockView* blocks = nullptr;
    std::size_t block_count = 0;
};

struct ClientInventoryView {
    InventoryState inventory{};
};

struct ClientImageInfo {
    std::uintptr_t base = 0;
    std::size_t mapped_size = 0;
    std::string library_name;
    std::string build_id_hex;
    std::uint8_t elf_class = 0;
    std::uint16_t machine = 0;
    std::vector<ClientExecutableSegment> executable_segments;
};

struct EmbeddedClientHostApi {
    std::uint32_t abi_version = kEmbeddedClientAbiVersion;
    std::uint32_t protocol = kEmbeddedClientProtocol;
    const char* game_version = kEmbeddedClientGameVersion;
    const char* library_name = kEmbeddedClientLibrary;
    void* user = nullptr;

    int (*read_client_state)(void* user, ClientStateView* out) = nullptr;
    int (*read_inventory)(void* user, ClientInventoryView* out) = nullptr;
    int (*read_touch)(void* user, EmbeddedTouchInput* out) = nullptr;
    int (*compute_ai)(void* user, const WorldSnapshot* snapshot, const InventoryState* inventory, EmbeddedAiInput* out) = nullptr;
    void (*tick_identity)(void* client_context) = nullptr;
    int (*submit_actions)(void* user, const EmbeddedActionFrame* actions) = nullptr;

    // Optional: a modified/embedded client may use this to register our tick callback
    // at its real client tick site instead of polling from Java.
    int (*register_tick_callback)(void* user,
                                   void (*callback)(void* callback_user),
                                   void* callback_user) = nullptr;

    int (*get_image_info)(void* user, ClientImageInfo* out) = nullptr;
    int (*install_client_hooks)(void* user, const ClientHookBinding* bindings, std::size_t count) = nullptr;
    ClientHookDispatchRegistrar register_hook_dispatch = nullptr;
    ClientNativeAbiCertificateReader get_native_abi_certificate = nullptr;
};

class EmbeddedBedrockClientIntegration {
public:
    EmbeddedBedrockClientIntegration();
    ~EmbeddedBedrockClientIntegration();
    EmbeddedBedrockClientIntegration(const EmbeddedBedrockClientIntegration&) = delete;
    EmbeddedBedrockClientIntegration& operator=(const EmbeddedBedrockClientIntegration&) = delete;

    bool attach(const EmbeddedClientHostApi& api, std::string* error = nullptr);
    void detach();
    bool attached() const { return attached_; }

    bool tick(std::string* error = nullptr);
    bool install_tick_callback(std::string* error = nullptr);
    bool bind_verified_hooks(const std::vector<ClientHookSpec>& specs,
                             ClientHookInstaller installer,
                             void* user,
                             std::string* error = nullptr);
    bool install_complete_hook_plan(const std::vector<ClientHookSpec>& specs,
                                    std::string* error = nullptr);
    bool hook_kind_installed(ClientHookKind kind) const;
    bool install_verified_tick_hook(const ClientHookSpec& spec, std::string* error = nullptr);
    bool live_tick_hook_installed() const { return tick_hook_.installed; }
    bool hook_dispatch_registered() const { return hook_dispatch_registered_; }

    const ClientImageInfo& image_info() const { return image_info_; }
    const EmbeddedActionFrame& last_actions() const { return last_actions_; }
    std::uint64_t tick_count() const { return tick_count_.load(std::memory_order_acquire); }
    const std::string& last_error() const { return last_error_; }

private:
    static void tick_thunk(void* user);
    static void tick_dispatch(void* user, Arm64RegisterFrame* frame);
    void tick_from_host();
    static int host_hook_installer(void* user, const ClientHookBinding* bindings, std::size_t count, std::string* error);
    static int hook_dispatch_tick(void* user, const ClientHookEventMetadata* metadata);
    static int hook_dispatch_state(void* user, const ClientHookEventMetadata* metadata, const ClientStateView* state);
    static int hook_dispatch_inventory(void* user, const ClientHookEventMetadata* metadata, const ClientInventoryView* inventory);
    static int hook_dispatch_touch(void* user, const ClientHookEventMetadata* metadata, const EmbeddedTouchInput* touch);
    static int hook_dispatch_actions(void* user, const ClientHookEventMetadata* metadata, const EmbeddedActionFrame* actions);
    bool register_hook_dispatch(std::string* error);
    bool validate_native_abi_certificate(std::string* error);
    bool enter_hook_dispatch();
    void leave_hook_dispatch();
    bool copy_state(const ClientStateView& client, WorldSnapshot& snapshot, std::string* error);

    EmbeddedClientHostApi api_{};
    EmbeddedClientControllerBridge bridge_{};
    ClientImageInfo image_info_{};
    EmbeddedActionFrame last_actions_{};
    std::string last_error_;
    std::atomic<std::uint64_t> tick_count_{0};
    Arm64InlineHookRuntime hook_runtime_{};
    Arm64InlineHookHandle tick_hook_{};
    std::vector<ClientHookBinding> verified_bindings_{};
    std::unique_ptr<ClientHookDispatcher> hook_dispatcher_{};
    bool hook_dispatch_registered_ = false;
    ClientNativeAbiCertificate native_abi_certificate_{};
    static constexpr std::uint32_t kHookDispatchDisabledBit = 0x80000000U;
    std::atomic<std::uint32_t> hook_dispatch_state_{kHookDispatchDisabledBit};
    std::array<bool, 5> hook_kind_installed_{};
    bool attached_ = false;
};

bool inspect_current_client_image(ClientImageInfo& out, std::string* error = nullptr);
bool client_image_is_compatible(const ClientImageInfo& image, std::string* error = nullptr);
std::string format_client_image_report(const ClientImageInfo& image);

} // namespace bedrockai

extern "C" {

struct BedrockAiEmbeddedClientHost {
    std::uint32_t abi_version;
    std::uint32_t protocol;
    const char* game_version;
    const char* library_name;
    void* user;
    int (*read_client_state)(void* user, bedrockai::ClientStateView* out);
    int (*read_inventory)(void* user, bedrockai::ClientInventoryView* out);
    int (*read_touch)(void* user, bedrockai::EmbeddedTouchInput* out);
    int (*compute_ai)(void* user, const bedrockai::WorldSnapshot* snapshot, const bedrockai::InventoryState* inventory, bedrockai::EmbeddedAiInput* out);
    void (*tick_identity)(void* client_context);
    int (*submit_actions)(void* user, const bedrockai::EmbeddedActionFrame* actions);
    int (*register_tick_callback)(void* user, void (*callback)(void* callback_user), void* callback_user);
    int (*get_image_info)(void* user, bedrockai::ClientImageInfo* out);
    int (*install_client_hooks)(void* user, const bedrockai::ClientHookBinding* bindings, std::size_t count);
    bedrockai::ClientHookDispatchRegistrar register_hook_dispatch;
    bedrockai::ClientNativeAbiCertificateReader get_native_abi_certificate;
};

std::uintptr_t bedrockai_client_integration_create();
void bedrockai_client_integration_destroy(std::uintptr_t handle);
int bedrockai_client_integration_attach(std::uintptr_t handle,
                                         const BedrockAiEmbeddedClientHost* host,
                                         char* error_buffer,
                                         std::size_t error_buffer_size);
void bedrockai_client_integration_detach(std::uintptr_t handle);
int bedrockai_client_integration_install_tick(std::uintptr_t handle,
                                               char* error_buffer,
                                               std::size_t error_buffer_size);
int bedrockai_client_integration_tick(std::uintptr_t handle,
                                      char* error_buffer,
                                      std::size_t error_buffer_size);
std::uint64_t bedrockai_client_integration_tick_count(std::uintptr_t handle);
int bedrockai_client_integration_install_complete_hook_plan(std::uintptr_t handle,
                                                            const char* plan_text,
                                                            char* error_buffer,
                                                            std::size_t error_buffer_size);
int bedrockai_client_integration_hook_kind_installed(std::uintptr_t handle, std::uint8_t kind);
int bedrockai_client_integration_install_tick_hook(std::uintptr_t handle,
                                                    const char* required_build_id_hex,
                                                    const char* signature_text,
                                                    const char* expected_text,
                                                    char* error_buffer,
                                                    std::size_t error_buffer_size);
int bedrockai_current_client_image_report(char* buffer, std::size_t buffer_size);

}
