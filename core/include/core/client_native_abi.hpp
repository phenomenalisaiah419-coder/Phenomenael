#pragma once

#include "hook_binding.hpp"
#include "arm64_inline_hook.hpp"


#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <type_traits>

namespace bedrockai {

inline constexpr std::uint32_t kClientNativeAbiCertificateVersion = 1U;
inline constexpr std::uint32_t kClientNativeAbiProtocol = 2193U;
inline constexpr std::uint8_t kClientNativeAbiPointerWidth = 8U;
inline constexpr std::size_t kClientHookKindCount = 5U;

struct ClientNativeAbiCertificate {
    std::uint32_t version = kClientNativeAbiCertificateVersion;
    std::uint32_t protocol = kClientNativeAbiProtocol;
    std::uint8_t pointer_width_bytes = kClientNativeAbiPointerWidth;
    std::uint8_t reserved[3] = {0U, 0U, 0U};
    const char* build_id_hex = nullptr;
    std::uintptr_t hook_target_addresses[kClientHookKindCount] = {};
    std::uintptr_t translator_addresses[kClientHookKindCount] = {};
};

static_assert(std::is_standard_layout_v<ClientNativeAbiCertificate>);
static_assert(offsetof(ClientNativeAbiCertificate, version) == 0U);
static_assert(offsetof(ClientNativeAbiCertificate, protocol) == 4U);
static_assert(offsetof(ClientNativeAbiCertificate, pointer_width_bytes) == 8U);

using ClientNativeAbiCertificateReader = int (*)(void* user, ClientNativeAbiCertificate* out,
                                                  std::string* error);

class ClientNativeAbiValidator {
public:
    bool validate(const ClientImageInfo& image,
                  const std::vector<ClientHookBinding>& bindings,
                  const ClientNativeAbiCertificate& certificate,
                  std::string* error = nullptr) const;

private:
    static bool valid_build_id(const char* build_id);
};

} // namespace bedrockai
