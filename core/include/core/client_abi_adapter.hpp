#pragma once

#include "embedded_client_bridge.hpp"
#include "client_native_frame.hpp"
#include "bedrock_client_integration.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>

namespace bedrockai {

inline constexpr std::uint32_t kClientAbiAdapterVersion = 1U;
inline constexpr std::uint8_t kAarch64PointerWidthBytes = 8U;

// These callback types are deliberately typed.  The exact 1.26.50 ARM64
// function signature must be supplied by the modified client after it has
// verified the actual libminecraftpe.so build and hook target.  We never cast
// an arbitrary address to one of these types inside the core.
using ClientTickFn = void (*)(void* client_context);
using ClientReadStateFn = int (*)(void* client_context, ClientStateView* out);
using ClientReadInventoryFn = int (*)(void* client_context, ClientInventoryView* out);
using ClientReadTouchFn = int (*)(void* client_context, EmbeddedTouchInput* out);
using ClientSubmitActionsFn = int (*)(void* client_context, const EmbeddedActionFrame* actions);

struct ClientAbiTable {
    std::uint32_t version = kClientAbiAdapterVersion;
    std::uint32_t protocol = kEmbeddedClientProtocol;
    std::uint8_t pointer_width_bytes = kAarch64PointerWidthBytes;
    std::uint8_t reserved[3] = {0U, 0U, 0U};
    void* client_context = nullptr;
    ClientTickFn tick = nullptr;
    ClientReadStateFn read_state = nullptr;
    ClientReadInventoryFn read_inventory = nullptr;
    ClientReadTouchFn read_touch = nullptr;
    ClientSubmitActionsFn submit_actions = nullptr;
};

static_assert(std::is_standard_layout_v<ClientAbiTable>);
static_assert(offsetof(ClientAbiTable, version) == 0U);
static_assert(offsetof(ClientAbiTable, protocol) == 4U);
static_assert(offsetof(ClientAbiTable, pointer_width_bytes) == 8U);
static_assert(offsetof(ClientAbiTable, client_context) % alignof(void*) == 0U);

class ClientAbiAdapter {
public:
    bool attach(const ClientAbiTable& table, std::string* error = nullptr);
    void detach();
    bool attached() const { return attached_; }

    bool invoke_read_state(ClientStateView& out, std::string* error = nullptr) const;
    bool invoke_read_inventory(ClientInventoryView& out, std::string* error = nullptr) const;
    bool invoke_read_touch(EmbeddedTouchInput& out, std::string* error = nullptr) const;
    bool invoke_submit_actions(const EmbeddedActionFrame& actions, std::string* error = nullptr) const;

    // The tick target is kept for ABI identity/diagnostics.  The integration
    // invokes its own hook dispatcher at the verified tick address and must
    // never recursively call this pointer as the hook itself.
    ClientTickFn original_tick() const { return table_.tick; }
    void* client_context() const { return table_.client_context; }

    static bool validate(const ClientAbiTable& table, std::string* error = nullptr);

private:
    ClientAbiTable table_{};
    bool attached_ = false;
};

struct ClientAbiHookDispatchResult {
    bool state_read = false;
    bool inventory_read = false;
    bool touch_read = false;
    bool actions_submitted = false;
    std::string error;
};

// Runs the same-character controller path through typed client ABI adapters.
// This is the runtime seam used after the real client Tick hook has fired.
class ClientAbiTickRunner {
public:
    bool run(const ClientAbiAdapter& adapter,
             EmbeddedClientControllerBridge& bridge,
             const ClientAbiTable& table,
             const std::function<bool(const WorldSnapshot&, const InventoryState&, EmbeddedAiInput&, std::string*)>& compute_ai,
             EmbeddedActionFrame& output,
             ClientAbiHookDispatchResult& result) const;

    bool run_captured(const ClientNativeFrame& frame,
                      EmbeddedClientControllerBridge& bridge,
                      const std::function<bool(const WorldSnapshot&, const InventoryState&, EmbeddedAiInput&, std::string*)>& compute_ai,
                      EmbeddedActionFrame& output,
                      ClientAbiHookDispatchResult& result) const;
};

} // namespace bedrockai
