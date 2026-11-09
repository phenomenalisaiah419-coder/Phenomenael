#include "core/arm64_inline_hook.hpp"
#include "core/bedrock_client_integration.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace bedrockai {
namespace {

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
constexpr std::uint32_t kAarch64BtiC = 0xD503245FU;
constexpr std::uint32_t kAarch64LdrX16Literal8 = 0x58000050U;
constexpr std::uint32_t kAarch64LdrX17Literal8 = 0x58000051U;
constexpr std::uint32_t kAarch64BrX16 = 0xD61F0200U;
constexpr std::uint32_t kAarch64BrX17 = 0xD61F0220U;
constexpr std::size_t kPageFallback = 4096U;
constexpr std::size_t kAllocationAlignment = 4096U;
constexpr std::size_t kTrampolineBytes = 24U;
constexpr std::size_t kVeneerBytes = 16U;
#endif
constexpr std::int64_t kBranchMinBytes = -(static_cast<std::int64_t>(1) << 27);
constexpr std::int64_t kBranchMaxBytes = (static_cast<std::int64_t>(1) << 27) - 4;

void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) |
           (static_cast<std::uint32_t>(p[3]) << 24U);
}

void write_u32(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value & 0xffU);
    p[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    p[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    p[3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

bool add_address(std::uintptr_t base, std::size_t size, std::uintptr_t& end) {
    const auto size_u = static_cast<std::uintptr_t>(size);
    if (size_u > std::numeric_limits<std::uintptr_t>::max() - base) return false;
    end = base + size_u;
    return true;
}


bool is_pc_relative_instruction(std::uint32_t insn) {
    const std::uint32_t top6 = insn & 0xFC000000U;
    if (top6 == 0x14000000U || top6 == 0x94000000U) return true; // B / BL
    if ((insn & 0xFF000010U) == 0x54000000U) return true; // B.cond
    if ((insn & 0x7F000000U) == 0x34000000U) return true; // CBZ / CBNZ
    if ((insn & 0x7F000000U) == 0x36000000U) return true; // TBZ / TBNZ
    if ((insn & 0x9F000000U) == 0x10000000U) return true; // ADR / ADRP
    if ((insn & 0x3F000000U) == 0x18000000U) return true; // literal loads/PRFM
    if ((insn & 0x3F000000U) == 0x1C000000U) return true; // SIMD/fp literal loads
    return false;
}

bool is_indirect_branch_or_return(std::uint32_t insn) {
    if ((insn & 0xFFFFFC1FU) == 0xD61F0000U) return true; // BR
    if ((insn & 0xFFFFFC1FU) == 0xD63F0000U) return true; // BLR
    if ((insn & 0xFFFFFC1FU) == 0xD65F0000U) return true; // RET
    return false;
}

bool has_unsupported_pac(std::uint32_t insn) {
    switch (insn) {
    case 0xD65F0BFFU: // RETAA
    case 0xD65F0FFFU: // RETAB
        return true;
    default:
        return false;
    }
}

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
std::size_t page_size() {
    const long value = sysconf(_SC_PAGESIZE);
    return value > 0 ? static_cast<std::size_t>(value) : kPageFallback;
}

bool align_page_range(std::uintptr_t address, std::size_t size,
                      std::uintptr_t& page_start, std::size_t& page_length) {
    const std::size_t page = page_size();
    if (page == 0U || (page & (page - 1U)) != 0U) return false;
    const auto mask = static_cast<std::uintptr_t>(page - 1U);
    page_start = address & ~mask;
    const auto delta = address - page_start;
    if (size > std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(delta)) return false;
    const std::size_t total = static_cast<std::size_t>(delta) + size;
    if (total > std::numeric_limits<std::size_t>::max() - (page - 1U)) return false;
    page_length = (total + page - 1U) & ~(page - 1U);
    return page_length != 0U;
}

bool set_patch_protection(std::uintptr_t address, std::size_t size, int protection,
                          std::uintptr_t& page_start, std::size_t& page_length, std::string* error) {
    if (!align_page_range(address, size, page_start, page_length)) {
        set_error(error, "cannot align hook patch page range");
        return false;
    }
    if (mprotect(reinterpret_cast<void*>(page_start), page_length, protection) != 0) {
        set_error(error, std::string("mprotect failed: ") + std::strerror(errno));
        return false;
    }
    return true;
}

void clear_instruction_cache(std::uintptr_t address, std::size_t size) {
    auto* begin = reinterpret_cast<char*>(address);
    __builtin___clear_cache(begin, begin + size);
}

void* allocate_executable_near(std::uintptr_t target, std::size_t size, std::uintptr_t& address_out) {
    const std::size_t page = page_size();
    if (page == 0U || size == 0U) return nullptr;
    const std::size_t allocation_size = std::max(page, kAllocationAlignment);
    const std::uintptr_t max_distance = static_cast<std::uintptr_t>(1U) << 27U;
    const std::uintptr_t aligned_target = target & ~static_cast<std::uintptr_t>(page - 1U);
    const std::size_t step = std::max<std::size_t>(page, 1U << 20U);

    for (std::size_t i = 0; i <= 128U; ++i) {
        const std::uintptr_t delta = static_cast<std::uintptr_t>(i) * static_cast<std::uintptr_t>(step);
        if (delta > max_distance) break;
        const std::uintptr_t candidates[2] = {
            aligned_target <= delta ? aligned_target - delta : 0U,
            aligned_target <= std::numeric_limits<std::uintptr_t>::max() - delta
                ? aligned_target + delta : std::numeric_limits<std::uintptr_t>::max()
        };
        for (const auto candidate : candidates) {
            if (candidate == 0U) continue;
            void* mapped = mmap(reinterpret_cast<void*>(candidate), allocation_size,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mapped == MAP_FAILED) continue;
            const auto mapped_address = reinterpret_cast<std::uintptr_t>(mapped);
            std::array<std::uint8_t, 4> branch_test{};
            std::string branch_error;
            if (Arm64InlineHookRuntime::build_branch(target, mapped_address, branch_test, &branch_error)) {
                address_out = mapped_address;
                return mapped;
            }
            munmap(mapped, allocation_size);
        }
    }
    return nullptr;
}
#endif

} // namespace

bool client_executable_contains(const ClientImageInfo& image,
                                std::uintptr_t address,
                                std::size_t size) {
    if (size == 0U) return false;
    std::uintptr_t end_address = 0;
    if (!add_address(address, size, end_address)) return false;
    for (const auto& segment : image.executable_segments) {
        std::uintptr_t end = 0;
        if (!add_address(segment.start, segment.size, end)) return false;
        if (address >= segment.start && end_address <= end) return true;
    }
    return false;
}

bool Arm64InlineHookRuntime::is_pac_or_return_instruction(std::uint32_t instruction) {
    return has_unsupported_pac(instruction) || is_indirect_branch_or_return(instruction);
}

bool Arm64InlineHookRuntime::relocatable_first_instruction(std::uint32_t instruction, std::string* error) {
    if (has_unsupported_pac(instruction)) {
        set_error(error, "first instruction is a return instruction and cannot be relocated");
        return false;
    }
    if (is_pc_relative_instruction(instruction)) {
        set_error(error, "first instruction is PC-relative and is not safely relocatable");
        return false;
    }
    if (is_indirect_branch_or_return(instruction)) {
        set_error(error, "first instruction is control-flow and cannot be relocated");
        return false;
    }
    return true;
}

bool Arm64InlineHookRuntime::build_branch(std::uintptr_t source,
                                          std::uintptr_t target,
                                          std::array<std::uint8_t, 4>& out,
                                          std::string* error) {
    if ((source & 0x3U) != 0U || (target & 0x3U) != 0U) {
        set_error(error, "AArch64 branch addresses must be 4-byte aligned");
        return false;
    }
    std::int64_t delta = 0;
    if (target >= source) {
        const auto unsigned_delta = target - source;
        if (unsigned_delta > static_cast<std::uintptr_t>(kBranchMaxBytes)) {
            set_error(error, "AArch64 B target is outside the +/-128 MiB range");
            return false;
        }
        delta = static_cast<std::int64_t>(unsigned_delta);
    } else {
        const auto unsigned_delta = source - target;
        const auto min_distance = static_cast<std::uintptr_t>(-kBranchMinBytes);
        if (unsigned_delta > min_distance) {
            set_error(error, "AArch64 B target is outside the +/-128 MiB range");
            return false;
        }
        delta = -static_cast<std::int64_t>(unsigned_delta);
    }
    if ((delta % 4LL) != 0) {
        set_error(error, "AArch64 B target is outside the +/-128 MiB range");
        return false;
    }
    const auto imm = delta >> 2;
    const auto imm26 = static_cast<std::uint32_t>(imm) & 0x03ffffffU;
    write_u32(out.data(), 0x14000000U | imm26);
    return true;
}

bool Arm64InlineHookRuntime::install_tick(const ClientImageInfo& image,
                                          const ClientHookSpec& spec,
                                          const ClientHookBinding& binding,
                                          void* dispatch_user,
                                          void (*dispatch)(void* user, Arm64RegisterFrame* frame),
                                          Arm64InlineHookHandle& out,
                                          std::string* error) const {
    out = Arm64InlineHookHandle{};
    if (spec.kind != ClientHookKind::Tick) {
        set_error(error, "inline runtime only installs Tick hooks");
        return false;
    }
    if (spec.patch_offset != 0U || spec.patch_size_bytes != 4U) {
        set_error(error, "safe live Tick hook requires patch_offset=0 and patch_size_bytes=4");
        return false;
    }
    if (spec.expected_bytes.bytes.size() != 4U || spec.expected_bytes.mask.size() != 4U) {
        set_error(error, "Tick hook requires exactly one expected AArch64 instruction");
        return false;
    }
    if (!dispatch) {
        set_error(error, "Tick hook dispatch callback is missing");
        return false;
    }
    if (!client_executable_contains(image, binding.patch_address, 4U)) {
        set_error(error, "Tick hook target is outside an executable client segment");
        return false;
    }
    if ((binding.patch_address & 0x3U) != 0U) {
        set_error(error, "Tick hook target is not AArch64-instruction aligned");
        return false;
    }

    const auto* target_bytes = reinterpret_cast<const std::uint8_t*>(binding.patch_address);
    if (!match_signature_at(target_bytes, 4U, 0U, spec.expected_bytes)) {
        set_error(error, "Tick hook original instruction no longer matches expected bytes");
        return false;
    }
    const std::uint32_t original_instruction = read_u32(target_bytes);
    if (!relocatable_first_instruction(original_instruction, error)) return false;

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    const std::size_t mapping_page = page_size();
    if (mapping_page == 0U) {
        set_error(error, "invalid runtime page size");
        return false;
    }
    std::uintptr_t trampoline_address = 0;
    void* trampoline_memory = mmap(nullptr, mapping_page, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline_memory == MAP_FAILED) {
        set_error(error, std::string("trampoline mmap failed: ") + std::strerror(errno));
        return false;
    }
    trampoline_address = reinterpret_cast<std::uintptr_t>(trampoline_memory);

    std::uintptr_t veneer_address = 0;
    void* veneer_memory = allocate_executable_near(binding.patch_address, 16U, veneer_address);
    if (!veneer_memory) {
        munmap(trampoline_memory, mapping_page);
        set_error(error, "could not allocate a hook veneer within AArch64 B range");
        return false;
    }

    auto* trampoline = reinterpret_cast<std::uint8_t*>(trampoline_memory);
    auto* veneer = reinterpret_cast<std::uint8_t*>(veneer_memory);
    std::memset(trampoline, 0, 32U);
    std::memset(veneer, 0, 16U);

    auto* context = new (std::nothrow) Arm64TickHookContext{};
    if (!context) {
        munmap(veneer_memory, mapping_page);
        munmap(trampoline_memory, mapping_page);
        set_error(error, "allocation of Tick hook context failed");
        return false;
    }
    context->trampoline = trampoline_memory;
    context->dispatch_user.store(dispatch_user, std::memory_order_release);
    context->dispatch.store(dispatch, std::memory_order_release);

    std::array<std::uint8_t, 4> branch_to_veneer{};
    if (!build_branch(binding.patch_address, veneer_address, branch_to_veneer, error)) {
        delete context;
        munmap(veneer_memory, mapping_page);
        munmap(trampoline_memory, mapping_page);
        return false;
    }

    // Trampoline: BTI C; original first instruction; absolute branch to target+4.
    write_u32(trampoline + 0U, kAarch64BtiC);
    write_u32(trampoline + 4U, original_instruction);
    write_u32(trampoline + 8U, kAarch64LdrX17Literal8);
    write_u32(trampoline + 12U, kAarch64BrX17);
    const auto return_address = binding.patch_address + 4U;
    std::memcpy(trampoline + 16U, &return_address, sizeof(return_address));

    // Veneer: load context into x16 and enter universal tick stub.
    write_u32(veneer + 0U, kAarch64LdrX16Literal8);
    write_u32(veneer + 4U, kAarch64BrX16);
    const auto entry_address = reinterpret_cast<std::uintptr_t>(&bedrockai_arm64_tick_entry);
    std::memcpy(veneer + 8U, &entry_address, sizeof(entry_address));
    if (mprotect(reinterpret_cast<void*>(trampoline_address), mapping_page, PROT_READ | PROT_EXEC) != 0) {
        context->dispatch.store(nullptr, std::memory_order_release);
        delete context;
        munmap(veneer_memory, mapping_page);
        munmap(trampoline_memory, mapping_page);
        set_error(error, std::string("trampoline mprotect failed: ") + std::strerror(errno));
        return false;
    }
    clear_instruction_cache(trampoline_address, kTrampolineBytes);
    if (mprotect(reinterpret_cast<void*>(veneer_address), mapping_page, PROT_READ | PROT_EXEC) != 0) {
        context->dispatch.store(nullptr, std::memory_order_release);
        delete context;
        munmap(veneer_memory, mapping_page);
        munmap(trampoline_memory, mapping_page);
        set_error(error, std::string("veneer mprotect failed: ") + std::strerror(errno));
        return false;
    }
    clear_instruction_cache(veneer_address, kVeneerBytes);

    std::uintptr_t page_start = 0;
    std::size_t page_length = 0;
    const auto& segment = *std::find_if(image.executable_segments.begin(), image.executable_segments.end(),
        [&](const ClientExecutableSegment& candidate) {
            std::uintptr_t end = 0;
            return add_address(candidate.start, candidate.size, end) &&
                   binding.patch_address >= candidate.start && binding.patch_address + 4U <= end;
        });
    const int original_protection = ((segment.flags & 0x4U) != 0U ? PROT_READ : 0) |
                                    ((segment.flags & 0x2U) != 0U ? PROT_WRITE : 0) |
                                    ((segment.flags & 0x1U) != 0U ? PROT_EXEC : 0);
    if (!set_patch_protection(binding.patch_address, 4U, PROT_READ | PROT_WRITE | PROT_EXEC,
                              page_start, page_length, error)) {
        context->dispatch.store(nullptr, std::memory_order_release);
        delete context;
        munmap(veneer_memory, mapping_page);
        munmap(trampoline_memory, mapping_page);
        return false;
    }
    if (mprotect(reinterpret_cast<void*>(page_start), page_length, original_protection) != 0) {
        set_error(error, std::string("hook page protection preflight restore failed: ") + std::strerror(errno));
        context->dispatch.store(nullptr, std::memory_order_release);
        delete context;
        munmap(veneer_memory, mapping_page);
        munmap(trampoline_memory, mapping_page);
        return false;
    }
    if (!set_patch_protection(binding.patch_address, 4U, PROT_READ | PROT_WRITE | PROT_EXEC,
                              page_start, page_length, error)) {
        context->dispatch.store(nullptr, std::memory_order_release);
        delete context;
        munmap(veneer_memory, mapping_page);
        munmap(trampoline_memory, mapping_page);
        return false;
    }

    Arm64InlineHookHandle candidate{};
    candidate.target = binding.patch_address;
    candidate.veneer = veneer_address;
    candidate.trampoline = trampoline_address;
    candidate.trampoline_size = kTrampolineBytes;
    std::memcpy(candidate.original.data(), target_bytes, candidate.original.size());
    candidate.context = context;
    candidate.installed = false;
    std::memcpy(reinterpret_cast<void*>(binding.patch_address), branch_to_veneer.data(), branch_to_veneer.size());
    clear_instruction_cache(binding.patch_address, 4U);
    if (mprotect(reinterpret_cast<void*>(page_start), page_length, original_protection) != 0) {
        // The preflight restore succeeded, so this second restore failure is unexpected.
        // Keep the live hook and report it as a hard failure without freeing its context.
        out = candidate;
        out.installed = true;
        set_error(error, std::string("hook installed but final page protection restore failed: ") + std::strerror(errno));
        return false;
    }
    candidate.installed = true;
    out = candidate;
    return true;
#else
    (void)dispatch_user;
    (void)dispatch;
    set_error(error, "live AArch64 inline hooks require an Android/Linux AArch64 process");
    return false;
#endif
}

} // namespace bedrockai

extern "C" void bedrockai_arm64_tick_dispatch(void* raw_context, bedrockai::Arm64RegisterFrame* frame) {
    auto* context = static_cast<bedrockai::Arm64TickHookContext*>(raw_context);
    if (!context) return;
    context->in_flight.fetch_add(1U, std::memory_order_acquire);
    const auto dispatch = context->dispatch.load(std::memory_order_acquire);
    if (dispatch) {
        void* const user = context->dispatch_user.load(std::memory_order_acquire);
        dispatch(user, frame);
    }
    context->in_flight.fetch_sub(1U, std::memory_order_release);
}
