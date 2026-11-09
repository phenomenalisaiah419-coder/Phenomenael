#include "core/client_native_abi.hpp"
#include "core/bedrock_client_integration.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bedrockai;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"CHECK failed: %s at %d\n",#c,__LINE__); std::abort(); } } while(0)

static ClientImageInfo make_image(std::array<std::uint8_t, 64>& bytes) {
    ClientImageInfo image;
    image.library_name = kEmbeddedClientLibrary;
    image.base = reinterpret_cast<std::uintptr_t>(bytes.data());
    image.mapped_size = bytes.size();
    image.elf_class = 2U;
    image.machine = 183U;
    image.build_id_hex = "aabbccdd";
    image.executable_segments.push_back({image.base, bytes.size(), 5U});
    return image;
}

static std::vector<ClientHookBinding> make_bindings(const ClientImageInfo& image) {
    std::vector<ClientHookBinding> bindings;
    for (std::size_t i = 0; i < kClientHookKindCount; ++i) {
        bindings.push_back(ClientHookBinding{
            "hook" + std::to_string(i),
            static_cast<ClientHookKind>(i),
            image.base + i * 4U,
            image.base + i * 4U});
    }
    return bindings;
}

static ClientNativeAbiCertificate valid_certificate(const ClientImageInfo& image) {
    ClientNativeAbiCertificate certificate;
    certificate.build_id_hex = "aabbccdd";
    for (std::size_t i = 0; i < kClientHookKindCount; ++i) {
        certificate.hook_target_addresses[i] = image.base + i * 4U;
        certificate.translator_addresses[i] = image.base + 32U + i;
    }
    return certificate;
}

int main() {
    std::array<std::uint8_t, 64> bytes{};
    const auto image = make_image(bytes);
    const auto bindings = make_bindings(image);
    ClientNativeAbiValidator validator;
    std::string error;

    auto certificate = valid_certificate(image);
    CHECK(validator.validate(image, bindings, certificate, &error));

    certificate.build_id_hex = "deadbeef";
    CHECK(!validator.validate(image, bindings, certificate, &error));
    CHECK(error.find("build ID") != std::string::npos);

    certificate = valid_certificate(image);
    certificate.hook_target_addresses[2] = image.base + 20U;
    CHECK(!validator.validate(image, bindings, certificate, &error));
    CHECK(error.find("hook targets") != std::string::npos);

    certificate = valid_certificate(image);
    certificate.translator_addresses[3] = image.base + 63U;
    CHECK(validator.validate(image, bindings, certificate, &error));

    certificate = valid_certificate(image);
    certificate.translator_addresses[4] = 0U;
    CHECK(!validator.validate(image, bindings, certificate, &error));
    CHECK(error.find("missing a hook translator") != std::string::npos);

    certificate = valid_certificate(image);
    certificate.translator_addresses[4] = certificate.translator_addresses[3];
    CHECK(!validator.validate(image, bindings, certificate, &error));
    CHECK(error.find("reuses one translator") != std::string::npos);

    certificate = valid_certificate(image);
    certificate.pointer_width_bytes = 4U;
    CHECK(!validator.validate(image, bindings, certificate, &error));
    CHECK(error.find("64-bit") != std::string::npos);

    return 0;
}
