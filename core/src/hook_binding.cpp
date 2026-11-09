#include "core/hook_binding.hpp"
#include "core/bedrock_client_integration.hpp"

#include <limits>
#include <utility>

namespace bedrockai {
namespace {

constexpr std::uint16_t kAarch64ElfMachine = 183U;

void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

bool add_offset(std::uintptr_t base, std::size_t offset, std::uintptr_t& out) {
    const auto converted = static_cast<std::uintptr_t>(offset);
    if (converted > std::numeric_limits<std::uintptr_t>::max() - base) return false;
    out = base + converted;
    return true;
}

} // namespace

bool Arm64HookBinder::bind(const ClientImageInfo& image,
                           const std::vector<ClientHookSpec>& specs,
                           std::vector<ClientHookBinding>& out,
                           std::string* error) const {
    out.clear();
    if (specs.empty()) {
        set_error(error, "hook plan contains no hook specifications");
        return false;
    }
    if (image.library_name != kEmbeddedClientLibrary) {
        set_error(error, "hook binding requires libminecraftpe.so");
        return false;
    }
    if (image.base == 0U || image.mapped_size == 0U) {
        set_error(error, "client image range is invalid");
        return false;
    }
    if (image.elf_class != 2U || image.machine != kAarch64ElfMachine) {
        set_error(error, "loaded client image is not an ELF64 AArch64 image");
        return false;
    }
    if (image.build_id_hex.empty()) {
        set_error(error, "loaded client image has no GNU build ID");
        return false;
    }
    if (image.executable_segments.empty()) {
        set_error(error, "loaded client image has no executable segments");
        return false;
    }

    out.reserve(specs.size());
    for (const auto& spec : specs) {
        if (spec.name.empty()) {
            set_error(error, "hook specification has no name");
            out.clear();
            return false;
        }
        if (!spec.required_build_id_hex.empty() && spec.required_build_id_hex != image.build_id_hex) {
            set_error(error, "hook '" + spec.name + "' build ID mismatch");
            out.clear();
            return false;
        }
        SignatureMatch match{};
        if (!resolve_unique_signature(image.executable_segments, spec.signature, match, error)) {
            if (error && !error->empty()) *error = "hook '" + spec.name + "': " + *error;
            out.clear();
            return false;
        }
        std::uintptr_t patch_address = 0;
        if (!add_offset(match.address, spec.patch_offset, patch_address)) {
            set_error(error, "hook '" + spec.name + "' patch address overflow");
            out.clear();
            return false;
        }
        if (spec.patch_size_bytes == 0U) {
            set_error(error, "hook '" + spec.name + "' patch size must be non-zero");
            out.clear();
            return false;
        }
        const auto& segment = image.executable_segments[match.segment_index];
        if (patch_address < segment.start) {
            set_error(error, "hook '" + spec.name + "' patch address precedes executable segment");
            out.clear();
            return false;
        }
        const auto offset64 = patch_address - segment.start;
        if (offset64 > static_cast<std::uintptr_t>(segment.size)) {
            set_error(error, "hook '" + spec.name + "' patch address exceeds executable segment");
            out.clear();
            return false;
        }
        const auto patch_offset = static_cast<std::size_t>(offset64);
        if (spec.patch_size_bytes > segment.size - patch_offset) {
            set_error(error, "hook '" + spec.name + "' patch range exceeds executable segment");
            out.clear();
            return false;
        }
        if (!spec.expected_bytes.empty()) {
            const auto* data = reinterpret_cast<const std::uint8_t*>(segment.start);
            if (!match_signature_at(data, segment.size, patch_offset, spec.expected_bytes)) {
                set_error(error, "hook '" + spec.name + "' expected bytes mismatch");
                out.clear();
                return false;
            }
        }
        for (const auto& prior : out) {
            if (prior.patch_address == patch_address) {
                set_error(error, "hook plan contains overlapping patch addresses");
                out.clear();
                return false;
            }
        }
        out.push_back(ClientHookBinding{spec.name, spec.kind, match.address, patch_address});
    }
    return true;
}

bool install_verified_client_hooks(const ClientImageInfo& image,
                                   const std::vector<ClientHookSpec>& specs,
                                   ClientHookInstaller installer,
                                   void* user,
                                   std::string* error) {
    if (!installer) {
        set_error(error, "client hook installer callback is missing");
        return false;
    }
    std::vector<ClientHookBinding> bindings;
    Arm64HookBinder binder;
    if (!binder.bind(image, specs, bindings, error)) return false;
    if (installer(user, bindings.data(), bindings.size(), error) == 0) {
        if (!error || error->empty()) set_error(error, "client hook installer rejected verified bindings");
        return false;
    }
    return true;
}

} // namespace bedrockai
