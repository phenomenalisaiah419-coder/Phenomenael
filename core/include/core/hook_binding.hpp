#pragma once

#include "aarch64_signature.hpp"
#include "client_hook_event.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bedrockai {

struct ClientImageInfo;
struct ClientStateView;
struct ClientInventoryView;
struct EmbeddedTouchInput;
struct EmbeddedActionFrame;

enum class ClientHookKind : std::uint8_t {
    Tick = 0,
    ReadState = 1,
    ReadInventory = 2,
    ReadTouch = 3,
    SubmitActions = 4,
};

struct ClientHookSpec {
    std::string name;
    ClientHookKind kind = ClientHookKind::Tick;
    std::string required_build_id_hex;
    SignaturePattern signature;
    std::size_t patch_offset = 0;
    std::size_t patch_size_bytes = 4;
    SignaturePattern expected_bytes;
};

struct ClientHookBinding {
    std::string name;
    ClientHookKind kind = ClientHookKind::Tick;
    std::uintptr_t signature_address = 0;
    std::uintptr_t patch_address = 0;
};

class Arm64HookBinder {
public:
    bool bind(const ClientImageInfo& image,
              const std::vector<ClientHookSpec>& specs,
              std::vector<ClientHookBinding>& out,
              std::string* error = nullptr) const;
};

using ClientHookInstaller = int (*)(void* user, const ClientHookBinding* binding, std::size_t count, std::string* error);

// Stable, translated event ABI between the verified client hooks and the controller.
// The modified client owns the real Mojang function signatures and must translate
// them into these typed events; the controller never casts a raw hook address.
inline constexpr std::uint32_t kClientHookDispatchAbiVersion = 2U;
struct ClientHookDispatchRegistration {
    std::uint32_t abi_version = kClientHookDispatchAbiVersion;
    void* user = nullptr;
    int (*on_tick)(void* user, const ClientHookEventMetadata* metadata) = nullptr;
    int (*on_read_state)(void* user, const ClientHookEventMetadata* metadata, const ClientStateView* state) = nullptr;
    int (*on_read_inventory)(void* user, const ClientHookEventMetadata* metadata, const ClientInventoryView* inventory) = nullptr;
    int (*on_read_touch)(void* user, const ClientHookEventMetadata* metadata, const EmbeddedTouchInput* touch) = nullptr;
    int (*on_submit_actions)(void* user, const ClientHookEventMetadata* metadata, const EmbeddedActionFrame* actions) = nullptr;
};

using ClientHookDispatchRegistrar = int (*)(void* user, const ClientHookDispatchRegistration* registration, std::string* error);

bool install_verified_client_hooks(const ClientImageInfo& image,
                                   const std::vector<ClientHookSpec>& specs,
                                   ClientHookInstaller installer,
                                   void* user,
                                   std::string* error = nullptr);

} // namespace bedrockai
