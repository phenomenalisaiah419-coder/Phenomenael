#include "core/client_native_abi.hpp"
#include "core/bedrock_client_integration.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <utility>

namespace bedrockai {
namespace {

void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

bool same_address_set(const std::vector<ClientHookBinding>& bindings,
                      const ClientNativeAbiCertificate& certificate) {
    if (bindings.size() != kClientHookKindCount) return false;
    std::array<bool, kClientHookKindCount> seen{};
    for (const auto& binding : bindings) {
        const auto index = static_cast<std::size_t>(binding.kind);
        if (index >= kClientHookKindCount || seen[index]) return false;
        seen[index] = true;
        if (certificate.hook_target_addresses[index] != binding.patch_address) return false;
    }
    return std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
}

} // namespace

bool ClientNativeAbiValidator::valid_build_id(const char* build_id) {
    if (!build_id || *build_id == '\0') return false;
    std::size_t length = 0U;
    for (const char* cursor = build_id; *cursor != '\0'; ++cursor, ++length) {
        const unsigned char c = static_cast<unsigned char>(*cursor);
        if (!std::isxdigit(c)) return false;
        if (length > 128U) return false;
    }
    return (length % 2U) == 0U;
}

bool ClientNativeAbiValidator::validate(const ClientImageInfo& image,
                                         const std::vector<ClientHookBinding>& bindings,
                                         const ClientNativeAbiCertificate& certificate,
                                         std::string* error) const {
    if (certificate.version != kClientNativeAbiCertificateVersion) {
        set_error(error, "native client ABI certificate version mismatch");
        return false;
    }
    if (certificate.protocol != kClientNativeAbiProtocol) {
        set_error(error, "native client ABI certificate protocol mismatch");
        return false;
    }
    if (certificate.pointer_width_bytes != kClientNativeAbiPointerWidth) {
        set_error(error, "native client ABI certificate requires 64-bit pointers");
        return false;
    }
    if (!valid_build_id(certificate.build_id_hex)) {
        set_error(error, "native client ABI certificate has an invalid build ID");
        return false;
    }
    if (image.build_id_hex != certificate.build_id_hex) {
        set_error(error, "native client ABI certificate build ID does not match loaded image");
        return false;
    }
    if (!same_address_set(bindings, certificate)) {
        set_error(error, "native client ABI certificate hook targets do not match verified bindings");
        return false;
    }

    std::set<std::uintptr_t> translators;
    for (std::size_t index = 0U; index < kClientHookKindCount; ++index) {
        const auto translator = certificate.translator_addresses[index];
        if (translator == 0U) {
            set_error(error, "native client ABI certificate is missing a hook translator");
            return false;
        }
        if (!client_executable_contains(image, translator, 1U)) {
            set_error(error, "native client ABI translator is outside libminecraftpe.so executable segments");
            return false;
        }
        if (!translators.insert(translator).second) {
            set_error(error, "native client ABI certificate reuses one translator for multiple hook sites");
            return false;
        }
    }

    return true;
}

} // namespace bedrockai
