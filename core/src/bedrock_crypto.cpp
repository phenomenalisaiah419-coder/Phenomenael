#include "core/bedrock_crypto.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#if defined(BEDROCKAI_HAVE_OPENSSL)
#include <openssl/aes.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/param_build.h>
#include <openssl/params.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#endif

namespace bedrockai {
namespace {

void set_error(std::string* error, const char* message) {
    if (error) *error = message;
}

#if defined(BEDROCKAI_HAVE_OPENSSL)
void put_le64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (unsigned i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(value & 0xffu);
        value >>= 8;
    }
    out.insert(out.end(), bytes.begin(), bytes.end());
}

bool der_private_to_evp(const std::vector<std::uint8_t>& der, EVP_PKEY** out, std::string* error) {
    if (der.empty()) { set_error(error, "empty PKCS#8 private key"); return false; }
    const unsigned char* p = der.data();
    *out = d2i_AutoPrivateKey(nullptr, &p, static_cast<long>(der.size()));
    if (!*out) { set_error(error, "failed to parse PKCS#8 private key"); return false; }
    return true;
}

bool der_public_to_evp(const std::vector<std::uint8_t>& der, EVP_PKEY** out, std::string* error) {
    if (der.empty()) { set_error(error, "empty PKIX public key"); return false; }
    const unsigned char* p = der.data();
    *out = d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size()));
    if (!*out) { set_error(error, "failed to parse PKIX public key"); return false; }
    return true;
}

bool ensure_p384(EVP_PKEY* key, std::string* error) {
    if (!key) { set_error(error, "null EVP key"); return false; }
    if (EVP_PKEY_base_id(key) != EVP_PKEY_EC) { set_error(error, "expected ECDSA/ECDH key"); return false; }
    char group[80]{};
    std::size_t group_len = 0;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    if (EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME, group, sizeof(group), &group_len) <= 0) {
        set_error(error, "failed to inspect EC group");
        return false;
    }
    if (std::string(group, group_len) != "secp384r1") {
        set_error(error, "EC key is not secp384r1");
        return false;
    }
#else
    EC_KEY* ec = EVP_PKEY_get1_EC_KEY(key);
    if (!ec) { set_error(error, "failed to inspect EC key"); return false; }
    const EC_GROUP* grp = EC_KEY_get0_group(ec);
    const int nid = grp ? EC_GROUP_get_curve_name(grp) : NID_undef;
    EC_KEY_free(ec);
    if (nid != NID_secp384r1) { set_error(error, "EC key is not secp384r1"); return false; }
#endif
    return true;
}

std::vector<std::uint8_t> der_encode(EVP_PKEY* key, bool private_key) {
    if (!key) return {};
    if (private_key) {
        PKCS8_PRIV_KEY_INFO* p8 = EVP_PKEY2PKCS8(key);
        if (!p8) return {};
        const int size = i2d_PKCS8_PRIV_KEY_INFO(p8, nullptr);
        if (size <= 0) { PKCS8_PRIV_KEY_INFO_free(p8); return {}; }
        std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
        unsigned char* p = out.data();
        const int written = i2d_PKCS8_PRIV_KEY_INFO(p8, &p);
        PKCS8_PRIV_KEY_INFO_free(p8);
        if (written != size) return {};
        return out;
    }
    const int size = i2d_PUBKEY(key, nullptr);
    if (size <= 0) return {};
    std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
    unsigned char* p = out.data();
    if (i2d_PUBKEY(key, &p) != size) return {};
    return out;
}

#endif

} // namespace

#if defined(BEDROCKAI_HAVE_OPENSSL)

bool OpenSSLBedrockCryptoProvider::generate_p384_keypair(EcKeyPairDer& out, std::string* error) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!ctx) { set_error(error, "EVP_PKEY_CTX_new_id(EVP_PKEY_EC) failed"); return false; }
    EVP_PKEY* key = nullptr;
    bool ok = false;
    do {
        if (EVP_PKEY_keygen_init(ctx) <= 0) { set_error(error, "EC key generation init failed"); break; }
        if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_secp384r1) <= 0) { set_error(error, "P-384 curve selection failed"); break; }
        if (EVP_PKEY_keygen(ctx, &key) <= 0) { set_error(error, "P-384 key generation failed"); break; }
        if (!ensure_p384(key, error)) break;
        out.private_key_pkcs8_der = der_encode(key, true);
        out.public_key_pkix_der = der_encode(key, false);
        if (out.private_key_pkcs8_der.empty() || out.public_key_pkix_der.empty()) { set_error(error, "EC key DER encoding failed"); break; }
        ok = true;
    } while (false);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(ctx);
    return ok;
}

bool OpenSSLBedrockCryptoProvider::sign_es384(const std::vector<std::uint8_t>& private_key_pkcs8_der,
                                               const std::vector<std::uint8_t>& signing_input,
                                               std::vector<std::uint8_t>& signature_raw_rs,
                                               std::string* error) {
    EVP_PKEY* key = nullptr;
    if (!der_private_to_evp(private_key_pkcs8_der, &key, error)) return false;
    if (!ensure_p384(key, error)) { EVP_PKEY_free(key); return false; }
    EVP_MD_CTX* md = EVP_MD_CTX_new();
    EVP_PKEY_CTX* pctx = nullptr;
    bool ok = false;
    do {
        if (!md || EVP_DigestSignInit(md, &pctx, EVP_sha384(), nullptr, key) <= 0) {
            set_error(error, "ES384 signing init failed"); break;
        }
        if (EVP_DigestSignUpdate(md, signing_input.data(), signing_input.size()) <= 0) {
            set_error(error, "ES384 signing update failed"); break;
        }
        std::size_t der_len = 0;
        if (EVP_DigestSignFinal(md, nullptr, &der_len) <= 0 || der_len == 0) {
            set_error(error, "ES384 signature sizing failed"); break;
        }
        std::vector<std::uint8_t> der(der_len);
        if (EVP_DigestSignFinal(md, der.data(), &der_len) <= 0) {
            set_error(error, "ES384 signing failed"); break;
        }
        der.resize(der_len);
        const unsigned char* p = der.data();
        ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(der.size()));
        if (!sig) { set_error(error, "failed to decode DER ECDSA signature"); break; }
        const BIGNUM* r = nullptr;
        const BIGNUM* s = nullptr;
        ECDSA_SIG_get0(sig, &r, &s);
        signature_raw_rs.assign(96, 0);
        if (!r || !s || BN_bn2binpad(r, signature_raw_rs.data(), 48) != 48 ||
            BN_bn2binpad(s, signature_raw_rs.data() + 48, 48) != 48) {
            set_error(error, "failed to convert ES384 signature to JOSE r||s");
        } else {
            ok = true;
        }
        ECDSA_SIG_free(sig);
    } while (false);
    EVP_MD_CTX_free(md);
    EVP_PKEY_free(key);
    return ok;
}

bool OpenSSLBedrockCryptoProvider::verify_es384(const std::vector<std::uint8_t>& public_key_pkix_der,
                                                 const std::vector<std::uint8_t>& signing_input,
                                                 const std::vector<std::uint8_t>& signature_raw_rs,
                                                 std::string* error) {
    if (signature_raw_rs.size() != 96) { set_error(error, "ES384 JOSE signature must be 96 bytes"); return false; }
    EVP_PKEY* key = nullptr;
    if (!der_public_to_evp(public_key_pkix_der, &key, error)) return false;
    if (!ensure_p384(key, error)) { EVP_PKEY_free(key); return false; }
    ECDSA_SIG* sig = ECDSA_SIG_new();
    bool ok = false;
    do {
        BIGNUM* r = BN_bin2bn(signature_raw_rs.data(), 48, nullptr);
        BIGNUM* s = BN_bin2bn(signature_raw_rs.data() + 48, 48, nullptr);
        if (!sig || !r || !s || ECDSA_SIG_set0(sig, r, s) != 1) {
            BN_free(r); BN_free(s); set_error(error, "failed to construct ECDSA signature"); break;
        }
        int der_len = i2d_ECDSA_SIG(sig, nullptr);
        if (der_len <= 0) { set_error(error, "failed to size DER ECDSA signature"); break; }
        std::vector<std::uint8_t> der(static_cast<std::size_t>(der_len));
        unsigned char* p = der.data();
        if (i2d_ECDSA_SIG(sig, &p) != der_len) { set_error(error, "failed to encode DER ECDSA signature"); break; }

        EVP_MD_CTX* md = EVP_MD_CTX_new();
        EVP_PKEY_CTX* pctx = nullptr;
        if (!md || EVP_DigestVerifyInit(md, &pctx, EVP_sha384(), nullptr, key) <= 0 ||
            EVP_DigestVerifyUpdate(md, signing_input.data(), signing_input.size()) <= 0) {
            EVP_MD_CTX_free(md); set_error(error, "ES384 verify init failed"); break;
        }
        const int rc = EVP_DigestVerifyFinal(md, der.data(), der.size());
        EVP_MD_CTX_free(md);
        if (rc != 1) { set_error(error, "ES384 signature verification failed"); break; }
        ok = true;
    } while (false);
    ECDSA_SIG_free(sig);
    EVP_PKEY_free(key);
    return ok;
}

bool OpenSSLBedrockCryptoProvider::p384_shared_secret_x(const std::vector<std::uint8_t>& private_key_pkcs8_der,
                                                         const std::vector<std::uint8_t>& peer_public_key_pkix_der,
                                                         std::vector<std::uint8_t>& shared_secret_x48,
                                                         std::string* error) {
    EVP_PKEY* private_key = nullptr;
    EVP_PKEY* public_key = nullptr;
    if (!der_private_to_evp(private_key_pkcs8_der, &private_key, error)) return false;
    if (!der_public_to_evp(peer_public_key_pkix_der, &public_key, error)) { EVP_PKEY_free(private_key); return false; }
    bool ok = false;
    do {
        if (!ensure_p384(private_key, error) || !ensure_p384(public_key, error)) break;
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(private_key, nullptr);
        if (!ctx) { set_error(error, "ECDH context creation failed"); break; }
        std::size_t len = 0;
        if (EVP_PKEY_derive_init(ctx) <= 0 || EVP_PKEY_derive_set_peer(ctx, public_key) <= 0 ||
            EVP_PKEY_derive(ctx, nullptr, &len) <= 0 || len == 0) {
            EVP_PKEY_CTX_free(ctx); set_error(error, "P-384 ECDH setup failed"); break;
        }
        std::vector<std::uint8_t> secret(len);
        if (EVP_PKEY_derive(ctx, secret.data(), &len) <= 0) {
            EVP_PKEY_CTX_free(ctx); set_error(error, "P-384 ECDH failed"); break;
        }
        EVP_PKEY_CTX_free(ctx);
        // OpenSSL returns the X-coordinate for this EC primitive. Bedrock expects exactly 48 bytes.
        if (len > 48) { set_error(error, "unexpected P-384 shared-secret length"); break; }
        shared_secret_x48.assign(48, 0);
        const std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(48) - static_cast<std::ptrdiff_t>(len);
        std::copy(secret.begin(), secret.end(), shared_secret_x48.begin() + offset);
        ok = true;
    } while (false);
    EVP_PKEY_free(public_key);
    EVP_PKEY_free(private_key);
    return ok;
}

bool OpenSSLBedrockCryptoProvider::sha256(const std::vector<std::uint8_t>& data,
                                          std::array<std::uint8_t, 32>& digest,
                                          std::string* error) {
    if (SHA256(data.data(), data.size(), digest.data()) == nullptr) {
        set_error(error, "SHA-256 failed"); return false;
    }
    return true;
}

#else

bool OpenSSLBedrockCryptoProvider::generate_p384_keypair(EcKeyPairDer&, std::string* error) { set_error(error, "OpenSSL crypto provider unavailable"); return false; }
bool OpenSSLBedrockCryptoProvider::sign_es384(const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&, std::vector<std::uint8_t>&, std::string* error) { set_error(error, "OpenSSL crypto provider unavailable"); return false; }
bool OpenSSLBedrockCryptoProvider::verify_es384(const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&, std::string* error) { set_error(error, "OpenSSL crypto provider unavailable"); return false; }
bool OpenSSLBedrockCryptoProvider::p384_shared_secret_x(const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&, std::vector<std::uint8_t>&, std::string* error) { set_error(error, "OpenSSL crypto provider unavailable"); return false; }
bool OpenSSLBedrockCryptoProvider::sha256(const std::vector<std::uint8_t>&, std::array<std::uint8_t, 32>&, std::string* error) { set_error(error, "OpenSSL crypto provider unavailable"); return false; }

#endif

struct BedrockPacketCipher::Impl {
#if defined(BEDROCKAI_HAVE_OPENSSL)
    EVP_CIPHER_CTX* enc = nullptr;
    EVP_CIPHER_CTX* dec = nullptr;
#endif
    std::array<std::uint8_t, 32> key{};
};

BedrockPacketCipher::BedrockPacketCipher() : impl_(std::make_unique<Impl>()) {}
BedrockPacketCipher::~BedrockPacketCipher() {
#if defined(BEDROCKAI_HAVE_OPENSSL)
    EVP_CIPHER_CTX_free(impl_->enc);
    EVP_CIPHER_CTX_free(impl_->dec);
#endif
}
BedrockPacketCipher::BedrockPacketCipher(BedrockPacketCipher&& other) noexcept = default;
BedrockPacketCipher& BedrockPacketCipher::operator=(BedrockPacketCipher&& other) noexcept = default;

bool BedrockPacketCipher::enable(const std::array<std::uint8_t, 32>& key, std::string* error) {
#if defined(BEDROCKAI_HAVE_OPENSSL)
    EVP_CIPHER_CTX_free(impl_->enc);
    EVP_CIPHER_CTX_free(impl_->dec);
    impl_->enc = EVP_CIPHER_CTX_new();
    impl_->dec = EVP_CIPHER_CTX_new();
    if (!impl_->enc || !impl_->dec) { set_error(error, "AES cipher context allocation failed"); return false; }
    std::array<unsigned char, 16> iv{};
    std::copy(key.begin(), key.begin() + 12, iv.begin());
    iv[15] = 2; // current Bedrock/GopherTunnel initial CTR counter
    if (EVP_EncryptInit_ex(impl_->enc, EVP_aes_256_ctr(), nullptr, key.data(), iv.data()) != 1 ||
        EVP_DecryptInit_ex(impl_->dec, EVP_aes_256_ctr(), nullptr, key.data(), iv.data()) != 1) {
        set_error(error, "AES-256-CTR initialization failed");
        return false;
    }
    state_.key = key;
    state_.enabled = true;
    send_counter_ = 0;
    receive_counter_ = 0;
    return true;
#else
    (void)key; set_error(error, "AES packet crypto unavailable: build a crypto provider"); return false;
#endif
}

void BedrockPacketCipher::disable() {
    state_.enabled = false;
    state_.key.fill(0);
    send_counter_ = 0;
    receive_counter_ = 0;
#if defined(BEDROCKAI_HAVE_OPENSSL)
    EVP_CIPHER_CTX_free(impl_->enc); impl_->enc = nullptr;
    EVP_CIPHER_CTX_free(impl_->dec); impl_->dec = nullptr;
#endif
}

bool BedrockPacketCipher::encrypt_packet(std::vector<std::uint8_t>& packet, std::string* error) {
#if defined(BEDROCKAI_HAVE_OPENSSL)
    if (!state_.enabled || !impl_->enc) { set_error(error, "packet encryption is not enabled"); return false; }
    if (packet.empty()) { set_error(error, "cannot encrypt an empty Bedrock packet"); return false; }
    std::vector<std::uint8_t> checksum_input;
    checksum_input.reserve(8 + packet.size() - 1 + 32);
    put_le64(checksum_input, send_counter_);
    checksum_input.insert(checksum_input.end(), packet.begin() + 1, packet.end());
    checksum_input.insert(checksum_input.end(), state_.key.begin(), state_.key.end());
    std::array<std::uint8_t, 32> digest{};
    OpenSSLBedrockCryptoProvider provider;
    if (!provider.sha256(checksum_input, digest, error)) return false;
    packet.insert(packet.end(), digest.begin(), digest.begin() + 8);
    std::vector<std::uint8_t> encrypted(packet.size() - 1);
    int out_len = 0;
    int final_len = 0;
    if (EVP_EncryptUpdate(impl_->enc, encrypted.data(), &out_len, packet.data() + 1,
                          static_cast<int>(packet.size() - 1)) != 1 ||
        EVP_EncryptFinal_ex(impl_->enc, encrypted.data() + out_len, &final_len) != 1 ||
        static_cast<std::size_t>(out_len + final_len) != encrypted.size()) {
        set_error(error, "AES-256-CTR encryption failed"); return false;
    }
    std::copy(encrypted.begin(), encrypted.end(), packet.begin() + 1);
    ++send_counter_;
    return true;
#else
    (void)packet; set_error(error, "AES packet crypto unavailable"); return false;
#endif
}

bool BedrockPacketCipher::decrypt_packet(std::vector<std::uint8_t>& packet, std::string* error) {
#if defined(BEDROCKAI_HAVE_OPENSSL)
    if (!state_.enabled || !impl_->dec) { set_error(error, "packet decryption is not enabled"); return false; }
    if (packet.size() < 9) { set_error(error, "encrypted Bedrock packet is too short"); return false; }
    std::vector<std::uint8_t> decrypted(packet.size() - 1);
    int out_len = 0;
    int final_len = 0;
    if (EVP_DecryptUpdate(impl_->dec, decrypted.data(), &out_len, packet.data() + 1,
                          static_cast<int>(packet.size() - 1)) != 1 ||
        EVP_DecryptFinal_ex(impl_->dec, decrypted.data() + out_len, &final_len) != 1 ||
        static_cast<std::size_t>(out_len + final_len) != decrypted.size()) {
        set_error(error, "AES-256-CTR decryption failed"); return false;
    }
    const std::uint8_t header = packet[0];
    packet.resize(decrypted.size() + 1);
    packet[0] = header;
    std::copy(decrypted.begin(), decrypted.end(), packet.begin() + 1);
    if (packet.size() < 9) { set_error(error, "decrypted packet checksum is missing"); return false; }
    const std::size_t body_len = packet.size() - 1 - 8;
    std::vector<std::uint8_t> checksum_input;
    checksum_input.reserve(8 + body_len + 32);
    put_le64(checksum_input, receive_counter_);
    checksum_input.insert(checksum_input.end(), packet.begin() + 1, packet.begin() + 1 + static_cast<std::ptrdiff_t>(body_len));
    checksum_input.insert(checksum_input.end(), state_.key.begin(), state_.key.end());
    std::array<std::uint8_t, 32> digest{};
    OpenSSLBedrockCryptoProvider provider;
    if (!provider.sha256(checksum_input, digest, error)) return false;
    if (!std::equal(digest.begin(), digest.begin() + 8,
                    packet.begin() + 1 + static_cast<std::ptrdiff_t>(body_len))) {
        set_error(error, "Bedrock packet checksum mismatch");
        return false;
    }
    packet.resize(body_len + 1);
    ++receive_counter_;
    return true;
#else
    (void)packet; set_error(error, "AES packet crypto unavailable"); return false;
#endif
}

} // namespace bedrockai
