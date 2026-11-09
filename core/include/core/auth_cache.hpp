#pragma once

#include "core/login_payload.hpp"

#include <string>

namespace bedrockai {

// Loads the cache produced by tools/xbox_login.py. The cache is pinned to the
// exact server host:port in profile.json and its P-384 key is checked against
// the stored public key before the prebuilt login payload can be used.
class FileLoginCacheProvider final : public BedrockLoginAuthProvider,
                                     public BedrockPrebuiltLoginProvider {
public:
    explicit FileLoginCacheProvider(BedrockCryptoProvider& crypto,
                                    std::string cache_directory = {});

    bool get_credentials(const LoginAuthRequestContext& context,
                         LoginCredentialMaterial& out,
                         std::string* error = nullptr) override;

    bool get_prebuilt_login(const LoginAuthRequestContext& context,
                            LoginArtifact& out,
                            std::string* error = nullptr) override;

    const std::string& cache_directory() const { return cache_directory_; }

private:
    BedrockCryptoProvider& crypto_;
    std::string cache_directory_;
};

} // namespace bedrockai
