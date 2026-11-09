#include "core/bedrock_client_integration.hpp"
#include "core/hook_binding.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace bedrockai;

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "CHECK failed: %s at %d\n", #condition, __LINE__); std::abort(); } } while (0)

void test_signature_parser_and_wildcards() {
    SignaturePattern pattern;
    std::string error;
    CHECK(SignaturePattern::parse("AA BB ?? 0F", pattern, &error));
    CHECK(pattern.bytes.size() == 4U);
    CHECK(pattern.mask[2] == 0U);
    const std::uint8_t bytes[] = {0xAA, 0xBB, 0x91, 0x0F, 0x00};
    CHECK(match_signature_at(bytes, sizeof(bytes), 0U, pattern));
    CHECK(!match_signature_at(bytes, sizeof(bytes), 1U, pattern));
    CHECK(!SignaturePattern::parse("AA ZZ", pattern, &error));
}

void test_unique_resolver_and_duplicate_rejection() {
    std::uint8_t code[] = {0xAA,0xBB,0x01,0x00,0xAA,0xCC,0x01,0x00};
    SignaturePattern pattern;
    CHECK(SignaturePattern::parse("AA BB ?? 00", pattern));
    std::vector<ClientExecutableSegment> segments{{reinterpret_cast<std::uintptr_t>(code), sizeof(code), 0x5U}};
    SignatureMatch match{};
    std::string error;
    CHECK(resolve_unique_signature(segments, pattern, match, &error));
    CHECK(match.offset == 0U);

    SignaturePattern duplicate;
    CHECK(SignaturePattern::parse("AA ?? 01 00", duplicate));
    CHECK(!resolve_unique_signature(segments, duplicate, match, &error));
    CHECK(error.find("not unique") != std::string::npos);
}

void test_exact_build_id_and_expected_bytes_binding() {
    std::uint8_t code[] = {0x20,0x00,0x00,0x14,0x1F,0x20,0x03,0xD5};
    ClientImageInfo image;
    image.base = reinterpret_cast<std::uintptr_t>(code);
    image.mapped_size = sizeof(code);
    image.library_name = kEmbeddedClientLibrary;
    image.build_id_hex = "0123456789abcdef";
    image.elf_class = 2U;
    image.machine = 183U;
    image.executable_segments.push_back(ClientExecutableSegment{image.base, image.mapped_size, 0x5U});

    ClientHookSpec spec;
    spec.name = "tick";
    spec.kind = ClientHookKind::Tick;
    spec.required_build_id_hex = image.build_id_hex;
    CHECK(SignaturePattern::parse("20 00 00 14", spec.signature));
    CHECK(SignaturePattern::parse("20 00 00 14", spec.expected_bytes));

    Arm64HookBinder binder;
    std::vector<ClientHookBinding> bindings;
    std::string error;
    CHECK(binder.bind(image, {spec}, bindings, &error));
    CHECK(bindings.size() == 1U);
    CHECK(bindings[0].patch_address == image.base);

    spec.patch_size_bytes = sizeof(code) + 1U;
    CHECK(!binder.bind(image, {spec}, bindings, &error));
    CHECK(error.find("patch range") != std::string::npos);

    spec.patch_size_bytes = 4U;
    spec.required_build_id_hex = "wrong";
    CHECK(!binder.bind(image, {spec}, bindings, &error));
    CHECK(error.find("build ID mismatch") != std::string::npos);
}

struct InstallerCapture {
    std::vector<ClientHookBinding> received;
};

int installer(void* user, const ClientHookBinding* bindings, std::size_t count, std::string*) {
    auto* capture = static_cast<InstallerCapture*>(user);
    capture->received.assign(bindings, bindings + count);
    return 1;
}

void test_verified_install_boundary() {
    std::uint8_t code[] = {0x00,0x00,0x80,0xD2};
    ClientImageInfo image;
    image.base = reinterpret_cast<std::uintptr_t>(code);
    image.mapped_size = sizeof(code);
    image.library_name = kEmbeddedClientLibrary;
    image.build_id_hex = "deadbeef";
    image.elf_class = 2U;
    image.machine = 183U;
    image.executable_segments.push_back(ClientExecutableSegment{image.base, image.mapped_size, 0x5U});

    ClientHookSpec spec;
    spec.name = "state";
    spec.kind = ClientHookKind::ReadState;
    spec.required_build_id_hex = "deadbeef";
    CHECK(SignaturePattern::parse("00 00 80 D2", spec.signature));

    InstallerCapture capture;
    std::string error;
    CHECK(install_verified_client_hooks(image, {spec}, &installer, &capture, &error));
    CHECK(capture.received.size() == 1U);
    CHECK(capture.received[0].name == "state");
}

void test_image_report_and_compatibility() {
    ClientImageInfo image{};
    image.library_name = kEmbeddedClientLibrary;
    image.base = 0x1000;
    image.mapped_size = 0x1000;
    image.elf_class = 2U;
    image.machine = 183U;
    image.build_id_hex = "abcd";
    image.executable_segments.push_back(ClientExecutableSegment{image.base, image.mapped_size, 0x5U});
    std::string error;
    CHECK(client_image_is_compatible(image, &error));
    const auto report = format_client_image_report(image);
    CHECK(report.find("build_id=abcd") != std::string::npos);

    image.machine = 62U;
    CHECK(!client_image_is_compatible(image, &error));
    CHECK(error.find("AArch64") != std::string::npos);

#if defined(__ANDROID__) || defined(__linux__)
    char buffer[4096]{};
    const int rc = bedrockai_current_client_image_report(buffer, sizeof(buffer));
    CHECK(rc == 0 || rc == 1);
    if (rc == 0) CHECK(std::strlen(buffer) > 0U);
#endif
}


void test_report_c_api_is_fail_closed() {
#if defined(__ANDROID__) || defined(__linux__)
    char buffer[128]{};
    const int rc = bedrockai_current_client_image_report(buffer, sizeof(buffer));
    CHECK(rc == 0 || rc == 1);
    if (rc == 0) CHECK(std::strlen(buffer) > 0U);
#endif
}

void test_loaded_client_inspection_is_fail_closed() {
#if defined(__ANDROID__) || defined(__linux__)
    ClientImageInfo image;
    std::string error;
    const bool found = inspect_current_client_image(image, &error);
    if (!found) CHECK(error.find("libminecraftpe.so") != std::string::npos);
#else
    ClientImageInfo image;
    std::string error;
    CHECK(!inspect_current_client_image(image, &error));
#endif
}

int main() {
    test_signature_parser_and_wildcards();
    test_unique_resolver_and_duplicate_rejection();
    test_exact_build_id_and_expected_bytes_binding();
    test_verified_install_boundary();
    test_image_report_and_compatibility();
    test_report_c_api_is_fail_closed();
    test_loaded_client_inspection_is_fail_closed();
    return 0;
}
