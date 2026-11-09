#pragma once

#include <cstdint>
#include <type_traits>

namespace bedrockai {

inline constexpr std::uint32_t kClientHookEventAbiVersion = 1U;

// Exact tick identity attached by the modified client to every translated
// five-site event. The controller never infers the inventory/touch tick.
struct ClientHookEventMetadata {
    std::uint32_t version = kClientHookEventAbiVersion;
    std::uint32_t protocol = 2193U;
    std::uint64_t tick = 0U;
};

static_assert(std::is_standard_layout_v<ClientHookEventMetadata>);
static_assert(offsetof(ClientHookEventMetadata, version) == 0U);
static_assert(offsetof(ClientHookEventMetadata, protocol) == 4U);
static_assert(offsetof(ClientHookEventMetadata, tick) == 8U);

} // namespace bedrockai
