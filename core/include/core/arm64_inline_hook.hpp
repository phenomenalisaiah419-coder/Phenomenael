#pragma once

#include "hook_binding.hpp"

#include <array>
#include <cstddef>
#include <atomic>
#include <cstdint>
#include <string>

namespace bedrockai {

struct ClientImageInfo;

struct alignas(16) Arm64RegisterFrame {
    std::uint64_t gpr[31]{};
    std::uint8_t gpr_q_padding[8]{};
    std::uint8_t q[8][16]{};
    std::uint64_t nzcv = 0;
    void* context = nullptr;
};

struct Arm64TickHookContext {
    void* trampoline = nullptr;
    std::atomic<void*> dispatch_user{nullptr};
    std::atomic<void (*)(void* user, Arm64RegisterFrame* frame)> dispatch{nullptr};
    std::atomic<std::uint32_t> in_flight{0};
};

static_assert(offsetof(Arm64RegisterFrame, gpr) == 0U);
static_assert(offsetof(Arm64RegisterFrame, q) == 256U);
static_assert(offsetof(Arm64RegisterFrame, nzcv) == 384U);
static_assert(offsetof(Arm64RegisterFrame, context) == 392U);
static_assert(sizeof(Arm64RegisterFrame) == 400U);
static_assert(offsetof(Arm64TickHookContext, trampoline) == 0U);

struct Arm64InlineHookHandle {
    std::uintptr_t target = 0;
    std::uintptr_t veneer = 0;
    std::uintptr_t trampoline = 0;
    std::size_t trampoline_size = 0;
    std::array<std::uint8_t, 4> original{};
    Arm64TickHookContext* context = nullptr;
    bool installed = false;
};

class Arm64InlineHookRuntime {
public:
    bool install_tick(const ClientImageInfo& image,
                      const ClientHookSpec& spec,
                      const ClientHookBinding& binding,
                      void* dispatch_user,
                      void (*dispatch)(void* user, Arm64RegisterFrame* frame),
                      Arm64InlineHookHandle& out,
                      std::string* error = nullptr) const;

    static bool build_branch(std::uintptr_t source,
                             std::uintptr_t target,
                             std::array<std::uint8_t, 4>& out,
                             std::string* error = nullptr);

    static bool relocatable_first_instruction(std::uint32_t instruction, std::string* error = nullptr);

    static bool is_pac_or_return_instruction(std::uint32_t instruction);
};

bool client_executable_contains(const ClientImageInfo& image,
                                std::uintptr_t address,
                                std::size_t size);

} // namespace bedrockai

extern "C" void bedrockai_arm64_tick_dispatch(void* context, bedrockai::Arm64RegisterFrame* frame);
extern "C" void bedrockai_arm64_tick_entry();
