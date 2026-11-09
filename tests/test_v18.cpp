#include "core/arm64_inline_hook.hpp"
#include "core/bedrock_client_integration.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

using namespace bedrockai;

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "CHECK failed: %s at %d\n", #condition, __LINE__); std::abort(); } } while (0)

void test_branch_encoding() {
    std::array<std::uint8_t, 4> bytes{};
    std::string error;
    CHECK(Arm64InlineHookRuntime::build_branch(0x10000000U, 0x10000004U, bytes, &error));
    CHECK(bytes[0] == 0x01U && bytes[1] == 0x00U && bytes[2] == 0x00U && bytes[3] == 0x14U);
    CHECK(Arm64InlineHookRuntime::build_branch(0x10000004U, 0x10000000U, bytes, &error));
    CHECK(bytes[0] == 0xffU && bytes[1] == 0xffU && bytes[2] == 0xffU && bytes[3] == 0x17U);
    CHECK(!Arm64InlineHookRuntime::build_branch(0x10000000U, 0x18000000U, bytes, &error));
    CHECK(error.find("128 MiB") != std::string::npos);
    CHECK(!Arm64InlineHookRuntime::build_branch(0x10000002U, 0x10000006U, bytes, &error));
}

void test_relocation_guards() {
    std::string error;
    CHECK(Arm64InlineHookRuntime::relocatable_first_instruction(0xD503201FU, &error)); // NOP
    CHECK(Arm64InlineHookRuntime::relocatable_first_instruction(0xD503233FU, &error)); // PACIASP
    CHECK(!Arm64InlineHookRuntime::relocatable_first_instruction(0x10000000U, &error)); // ADR
    CHECK(error.find("PC-relative") != std::string::npos);
    CHECK(!Arm64InlineHookRuntime::relocatable_first_instruction(0xD65F0BFFU, &error)); // RETAA
    CHECK(error.find("return") != std::string::npos);
}

void test_real_tick_contract_rejects_unsafe_specs() {
    [[maybe_unused]] auto dispatch = [](void*, Arm64RegisterFrame*) {};
    Arm64InlineHookRuntime runtime;
    ClientImageInfo image;
    std::uint32_t instruction = 0xD503201FU;
    image.base = reinterpret_cast<std::uintptr_t>(&instruction);
    image.mapped_size = sizeof(instruction);
    image.library_name = kEmbeddedClientLibrary;
    image.build_id_hex = "deadbeef";
    image.elf_class = 2U;
    image.machine = 183U;
    image.executable_segments.push_back(ClientExecutableSegment{image.base, image.mapped_size, 0x5U});

    ClientHookSpec spec;
    spec.name = "client_tick";
    spec.kind = ClientHookKind::Tick;
    spec.patch_size_bytes = 8U;
    spec.required_build_id_hex = image.build_id_hex;
    CHECK(SignaturePattern::parse("1F 20 03 D5", spec.signature));
    CHECK(SignaturePattern::parse("1F 20 03 D5", spec.expected_bytes));
    ClientHookBinding binding{spec.name, spec.kind, image.base, image.base};
    Arm64InlineHookHandle out{};
    std::string error;
    CHECK(!runtime.install_tick(image, spec, binding, nullptr, nullptr, out, &error));
    CHECK(error.find("patch_size_bytes=4") != std::string::npos);

#if !defined(__aarch64__)
    spec.patch_size_bytes = 4U;
    CHECK(!runtime.install_tick(image, spec, binding, nullptr, dispatch, out, &error));
    CHECK(error.find("live AArch64 inline hooks") != std::string::npos);
#endif
}

void dispatch_counter(void* user, Arm64RegisterFrame*) {
    auto* count = static_cast<std::uint32_t*>(user);
    ++(*count);
}

void test_dispatch_guard() {
    Arm64TickHookContext context;
    Arm64RegisterFrame frame;
    std::uint32_t count = 0U;
    context.dispatch_user.store(&count, std::memory_order_release);
    context.dispatch.store(&dispatch_counter, std::memory_order_release);
    bedrockai_arm64_tick_dispatch(&context, &frame);
    CHECK(count == 1U);
    CHECK(context.in_flight.load(std::memory_order_acquire) == 0U);
    context.dispatch.store(nullptr, std::memory_order_release);
    bedrockai_arm64_tick_dispatch(&context, &frame);
    CHECK(count == 1U);
}

void test_executable_range_helper() {
    std::uint8_t bytes[32]{};
    ClientImageInfo image;
    image.executable_segments.push_back(ClientExecutableSegment{
        reinterpret_cast<std::uintptr_t>(bytes), sizeof(bytes), 0x5U});
    CHECK(client_executable_contains(image, reinterpret_cast<std::uintptr_t>(bytes) + 4U, 4U));
    CHECK(!client_executable_contains(image, reinterpret_cast<std::uintptr_t>(bytes) + 31U, 2U));
}

int main() {
    test_branch_encoding();
    test_relocation_guards();
    test_real_tick_contract_rejects_unsafe_specs();
    test_dispatch_guard();
    test_executable_range_helper();
    return 0;
}
