#include "core/client_process_runtime.hpp"
#include "core/hook_plan.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bedrockai;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "CHECK failed: %s at %d\n", #c, __LINE__); std::abort(); } } while (0)

void test_hook_plan_parse() {
    std::vector<ClientHookSpec> specs;
    std::string error;
    const std::string text =
        "# kind|name|build|signature|expected|offset|size\n"
        "Tick|client_tick|aabbccdd|1F 20 03 D5|1F 20 03 D5|0|4\n"
        "ReadState|client_state|aabbccdd|AA BB ??|AA BB 00|0|4\n";
    CHECK(parse_client_hook_plan(text, specs, &error));
    CHECK(specs.size() == 2U);
    CHECK(specs[0].kind == ClientHookKind::Tick);
    CHECK(specs[1].kind == ClientHookKind::ReadState);
    CHECK(specs[0].patch_size_bytes == 4U);
    CHECK(specs[1].signature.bytes.size() == 3U);
    CHECK(specs[1].signature.mask[2] == 0U);
}

void test_hook_plan_rejects_mixed_builds() {
    std::vector<ClientHookSpec> specs;
    std::string error;
    CHECK(!parse_client_hook_plan(
        "Tick|a|aa|1F 20 03 D5|1F 20 03 D5|0|4\n"
        "ReadState|b|bb|1F 20 03 D5|1F 20 03 D5|0|4\n", specs, &error));
    CHECK(error.find("multiple build IDs") != std::string::npos);
}

void test_hook_plan_requires_tick() {
    std::vector<ClientHookSpec> specs;
    std::string error;
    CHECK(!parse_client_hook_plan(
        "ReadState|state|aa|AA|AA|0|4\n", specs, &error));
    CHECK(error.find("does not contain a Tick") != std::string::npos);
}

void test_process_runtime_rejects_without_client_image() {
    ClientProcessRuntime runtime;
    std::string error;
    CHECK(!runtime.prepare({}, &error));
#if defined(__ANDROID__) || defined(__linux__)
    CHECK(error.find("libminecraftpe.so") != std::string::npos);
#else
    CHECK(error.find("client") != std::string::npos);
#endif
    CHECK(!runtime.prepared());
    CHECK(runtime.process_id() == 0U);
}

void test_process_runtime_bad_build_id() {
    ClientProcessRuntime runtime;
    std::string error;
    CHECK(!runtime.prepare("xyz", &error));
    CHECK(error.find("hexadecimal") != std::string::npos);
}

int main() {
    test_hook_plan_parse();
    test_hook_plan_rejects_mixed_builds();
    test_hook_plan_requires_tick();
    test_process_runtime_rejects_without_client_image();
    test_process_runtime_bad_build_id();
    return 0;
}
