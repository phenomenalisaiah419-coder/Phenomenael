#include "core/hook_plan.hpp"
#include "core/hook_binding.hpp"
#include "core/bedrock_client_integration.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bedrockai;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "CHECK failed: %s at %d\n", #c, __LINE__); std::abort(); } } while (0)

static std::string complete_plan() {
    return
        "Tick|tick|aabbccdd|80 81 82 83|80 81 82 83|0|4\n"
        "ReadState|state|aabbccdd|84 85 86 87|84 85 86 87|0|4\n"
        "ReadInventory|inventory|aabbccdd|88 89 8A 8B|88 89 8A 8B|0|4\n"
        "ReadTouch|touch|aabbccdd|8C 8D 8E 8F|8C 8D 8E 8F|0|4\n"
        "SubmitActions|actions|aabbccdd|90 91 92 93|90 91 92 93|0|4\n";
}

void test_complete_plan_validation() {
    std::vector<ClientHookSpec> specs;
    std::string error;
    CHECK(parse_client_hook_plan(complete_plan(), specs, &error));
    CHECK(specs.size() == 5U);
    CHECK(validate_complete_client_hook_plan(specs, "aabbccdd", &error));
}

void test_complete_plan_rejects_missing_site() {
    std::vector<ClientHookSpec> specs;
    std::string error;
    const std::string plan =
        "Tick|tick|aabbccdd|80 81 82 83|80 81 82 83|0|4\n"
        "ReadState|state|aabbccdd|84 85 86 87|84 85 86 87|0|4\n"
        "ReadInventory|inventory|aabbccdd|88 89 8A 8B|88 89 8A 8B|0|4\n"
        "ReadTouch|touch|aabbccdd|8C 8D 8E 8F|8C 8D 8E 8F|0|4\n";
    CHECK(parse_client_hook_plan(plan, specs, &error));
    CHECK(!validate_complete_client_hook_plan(specs, "aabbccdd", &error));
    CHECK(error.find("SubmitActions") != std::string::npos);
}

void test_complete_plan_rejects_duplicate_site() {
    std::vector<ClientHookSpec> specs;
    std::string error;
    const std::string plan = complete_plan() +
        "ReadState|state2|aabbccdd|15 25 35 45|15 25 35 45|0|4\n";
    CHECK(parse_client_hook_plan(plan, specs, &error));
    CHECK(!validate_complete_client_hook_plan(specs, "aabbccdd", &error));
    CHECK(error.find("multiple hooks") != std::string::npos);
}

void test_all_five_bindings_resolve() {
    std::array<std::uint8_t, 20> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(0x80U + i);
    ClientImageInfo image;
    image.library_name = kEmbeddedClientLibrary;
    image.base = reinterpret_cast<std::uintptr_t>(bytes.data());
    image.mapped_size = bytes.size();
    image.elf_class = 2U;
    image.machine = 183U;
    image.build_id_hex = "aabbccdd";
    image.executable_segments.push_back(ClientExecutableSegment{image.base, bytes.size(), 5U});

    std::vector<ClientHookSpec> specs;
    std::string error;
    CHECK(parse_client_hook_plan(
        "Tick|tick|aabbccdd|80 81 82 83|80 81 82 83|0|4\n"
        "ReadState|state|aabbccdd|84 85 86 87|84 85 86 87|0|4\n"
        "ReadInventory|inventory|aabbccdd|88 89 8A 8B|88 89 8A 8B|0|4\n"
        "ReadTouch|touch|aabbccdd|8C 8D 8E 8F|8C 8D 8E 8F|0|4\n"
        "SubmitActions|actions|aabbccdd|90 91 92 93|90 91 92 93|0|4\n", specs, &error));
    std::vector<ClientHookBinding> bindings;
    Arm64HookBinder binder;
    CHECK(binder.bind(image, specs, bindings, &error));
    CHECK(bindings.size() == 5U);
    std::array<bool, 5> seen{};
    for (const auto& b : bindings) {
        const auto index = static_cast<std::size_t>(b.kind);
        CHECK(index < seen.size());
        CHECK(!seen[index]);
        seen[index] = true;
    }
    for (const bool value : seen) CHECK(value);
}

struct HostProbe {
    std::array<std::uint8_t, 40> image_bytes{};
    std::vector<ClientHookBinding> installed{};
    ClientHookDispatchRegistration dispatch{};
};

int cb_state(void*, ClientStateView* out) {
    if (!out) return 0;
    *out = ClientStateView{};
    out->joined = true;
    out->tick = 1U;
    out->player.health = 20.0;
    return 1;
}
int cb_inventory(void*, ClientInventoryView* out) {
    if (!out) return 0;
    *out = ClientInventoryView{};
    return 1;
}
int cb_touch(void*, EmbeddedTouchInput* out) {
    if (!out) return 0;
    *out = EmbeddedTouchInput{};
    return 1;
}
int cb_ai(void*, const WorldSnapshot*, const InventoryState*, EmbeddedAiInput* out) {
    if (!out) return 0;
    *out = EmbeddedAiInput{};
    return 1;
}
int cb_submit(void*, const EmbeddedActionFrame*) { return 1; }

int cb_image(void* user, ClientImageInfo* out) {
    auto* host = static_cast<HostProbe*>(user);
    if (!host || !out) return 0;
    *out = ClientImageInfo{};
    out->base = reinterpret_cast<std::uintptr_t>(host->image_bytes.data());
    out->mapped_size = host->image_bytes.size();
    out->library_name = kEmbeddedClientLibrary;
    out->build_id_hex = "aabbccdd";
    out->elf_class = 2U;
    out->machine = 183U;
    out->executable_segments.push_back(ClientExecutableSegment{out->base, out->mapped_size, 5U});
    return 1;
}

int cb_install(void* user, const ClientHookBinding* bindings, std::size_t count) {
    auto* host = static_cast<HostProbe*>(user);
    if (!host || !bindings || count != 5U) return 0;
    host->installed.assign(bindings, bindings + count);
    return 1;
}

void cb_tick_identity(void*) {}

int cb_native_certificate(void* user, ClientNativeAbiCertificate* out, std::string* error) {
    auto* host = static_cast<HostProbe*>(user);
    if (!host || !out) { if (error) *error = "invalid native ABI certificate request"; return 0; }
    *out = ClientNativeAbiCertificate{};
    out->build_id_hex = "aabbccdd";
    for (std::size_t i = 0; i < kClientHookKindCount; ++i) {
        out->hook_target_addresses[i] = reinterpret_cast<std::uintptr_t>(host->image_bytes.data()) + i * 4U;
        out->translator_addresses[i] = reinterpret_cast<std::uintptr_t>(host->image_bytes.data()) + 24U + i;
    }
    return 1;
}

int cb_register_dispatch(void* user, const ClientHookDispatchRegistration* registration, std::string* error) {
    auto* host = static_cast<HostProbe*>(user);
    if (!host || !registration || registration->abi_version != kClientHookDispatchAbiVersion ||
        !registration->on_tick || !registration->on_read_state || !registration->on_read_inventory ||
        !registration->on_read_touch || !registration->on_submit_actions) {
        if (error) *error = "invalid dispatch registration";
        return 0;
    }
    host->dispatch = *registration;
    return 1;
}

void test_integration_installs_complete_plan_through_host_abi() {
    HostProbe host;
    for (std::size_t i = 0; i < host.image_bytes.size(); ++i) host.image_bytes[i] = static_cast<std::uint8_t>(0x80U + i);
    EmbeddedClientHostApi api;
    api.abi_version = kEmbeddedClientAbiVersion;
    api.protocol = kEmbeddedClientProtocol;
    api.game_version = kEmbeddedClientGameVersion;
    api.library_name = kEmbeddedClientLibrary;
    api.user = &host;
    api.read_client_state = cb_state;
    api.read_inventory = cb_inventory;
    api.read_touch = cb_touch;
    api.compute_ai = cb_ai;
    api.tick_identity = cb_tick_identity;
    api.submit_actions = cb_submit;
    api.get_image_info = cb_image;
    api.install_client_hooks = cb_install;
    api.register_hook_dispatch = cb_register_dispatch;
    api.get_native_abi_certificate = cb_native_certificate;

    EmbeddedBedrockClientIntegration runtime;
    std::string error;
    CHECK(runtime.attach(api, &error));
    std::vector<ClientHookSpec> specs;
    CHECK(parse_client_hook_plan(complete_plan(), specs, &error));
    if (!runtime.install_complete_hook_plan(specs, &error)) { std::fprintf(stderr, "install error: %s\n", error.c_str()); std::abort(); }
    CHECK(host.installed.size() == 5U);
    CHECK(runtime.hook_kind_installed(ClientHookKind::Tick));
    CHECK(runtime.hook_kind_installed(ClientHookKind::ReadState));
    CHECK(runtime.hook_kind_installed(ClientHookKind::ReadInventory));
    CHECK(runtime.hook_kind_installed(ClientHookKind::ReadTouch));
    CHECK(runtime.hook_kind_installed(ClientHookKind::SubmitActions));
    CHECK(host.dispatch.user != nullptr);
}

int main() {
    test_complete_plan_validation();
    test_complete_plan_rejects_missing_site();
    test_complete_plan_rejects_duplicate_site();
    test_all_five_bindings_resolve();
    test_integration_installs_complete_plan_through_host_abi();
    return 0;
}
