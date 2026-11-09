#include "core/auth_cache.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace bedrockai {
namespace {

constexpr std::size_t kMaxLoginPayloadBytes = 1024U * 1024U;
constexpr std::size_t kMaxProfileBytes = 64U * 1024U;
constexpr std::size_t kMaxPemBytes = 32U * 1024U;

void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

std::string default_cache_directory() {
    const char* configured = std::getenv("BEDROCK_AI_CACHE");
    if (configured && *configured) return configured;
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home || !*home) return {};
    std::string result(home);
    if (!result.empty() && result.back() != '/' && result.back() != '\\') result.push_back('/');
    result += ".bedrock_ai";
    return result;
}

bool read_file(const std::string& path, std::size_t max_bytes,
               std::vector<std::uint8_t>& out, std::string* error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        set_error(error, "cannot open login cache file: " + path);
        return false;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff length = stream.tellg();
    if (length <= 0 || static_cast<std::uint64_t>(length) > max_bytes) {
        set_error(error, "login cache file is empty or exceeds size limit: " + path);
        return false;
    }
    stream.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(length));
    stream.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!stream || static_cast<std::size_t>(stream.gcount()) != out.size()) {
        out.clear();
        set_error(error, "unable to read complete login cache file: " + path);
        return false;
    }
    return true;
}

bool read_text_file(const std::string& path, std::size_t max_bytes,
                    std::string& out, std::string* error) {
    std::vector<std::uint8_t> bytes;
    if (!read_file(path, max_bytes, bytes, error)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

bool read_json_string(const std::string& json, const std::string& field, std::string& value) {
    const std::string needle = "\"" + field + "\"";
    std::size_t pos = json.find(needle);
    while (pos != std::string::npos) {
        pos += needle.size();
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
        if (pos >= json.size() || json[pos] != ':') { pos = json.find(needle, pos); continue; }
        ++pos;
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
        if (pos >= json.size() || json[pos] != '"') { pos = json.find(needle, pos); continue; }
        ++pos;
        value.clear();
        while (pos < json.size()) {
            const char c = json[pos++];
            if (c == '"') return true;
            if (c != '\\') { value.push_back(c); continue; }
            if (pos >= json.size()) return false;
            const char escaped = json[pos++];
            switch (escaped) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: return false; // Cache metadata fields do not require Unicode escapes.
            }
        }
        return false;
    }
    return false;
}

bool read_json_integer(const std::string& json, const std::string& field, std::int64_t& value) {
    const std::string needle = "\"" + field + "\"";
    const std::size_t found = json.find(needle);
    if (found == std::string::npos) return false;
    std::size_t pos = json.find(':', found + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
    const std::size_t begin = pos;
    if (pos < json.size() && json[pos] == '-') ++pos;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])) != 0) ++pos;
    if (pos == begin) return false;
    try { value = std::stoll(json.substr(begin, pos - begin)); }
    catch (...) { return false; }
    return true;
}

int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

bool decode_base64(const std::string& input, std::vector<std::uint8_t>& out) {
    out.clear();
    std::uint32_t accumulator = 0U;
    unsigned bits = 0U;
    bool padding_started = false;
    for (char c : input) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) continue;
        if (c == '=') { padding_started = true; continue; }
        if (padding_started) return false;
        const int value = base64_value(c);
        if (value < 0) return false;
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
        bits += 6U;
        if (bits >= 8U) {
            bits -= 8U;
            out.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xffU));
        }
    }
    if (bits >= 6U || (bits != 0U && (accumulator & ((1U << bits) - 1U)) != 0U)) return false;
    return !out.empty();
}

bool decode_pkcs8_pem(const std::string& pem, std::vector<std::uint8_t>& der, std::string* error) {
    constexpr const char* kBegin = "-----BEGIN PRIVATE KEY-----";
    constexpr const char* kEnd = "-----END PRIVATE KEY-----";
    const std::size_t begin = pem.find(kBegin);
    const std::size_t end = pem.find(kEnd);
    if (begin == std::string::npos || end == std::string::npos || end <= begin + std::char_traits<char>::length(kBegin)) {
        set_error(error, "cached identity key is not PKCS#8 PRIVATE KEY PEM");
        return false;
    }
    const std::size_t content_start = begin + std::char_traits<char>::length(kBegin);
    if (!decode_base64(pem.substr(content_start, end - content_start), der)) {
        set_error(error, "cached identity key contains invalid PEM base64");
        return false;
    }
    if (der.size() < 32U || der.front() != 0x30U) {
        set_error(error, "cached identity key is not a DER sequence");
        return false;
    }
    static const std::vector<std::uint8_t> ec_public_key_oid{0x06U,0x07U,0x2aU,0x86U,0x48U,0xceU,0x3dU,0x02U,0x01U};
    static const std::vector<std::uint8_t> secp384r1_oid{0x06U,0x05U,0x2bU,0x81U,0x04U,0x00U,0x22U};
    const auto contains = [&der](const std::vector<std::uint8_t>& needle) {
        return std::search(der.begin(), der.end(), needle.begin(), needle.end()) != der.end();
    };
    if (!contains(ec_public_key_oid) || !contains(secp384r1_oid)) {
        set_error(error, "cached identity key is not an EC secp384r1/P-384 PKCS#8 key");
        return false;
    }
    return true;
}

bool validate_login_payload(const std::vector<std::uint8_t>& payload, std::string* error) {
    if (payload.size() < 6U) {
        set_error(error, "cached Login payload is too short");
        return false;
    }
    const std::uint32_t protocol = (static_cast<std::uint32_t>(payload[0]) << 24U) |
                                   (static_cast<std::uint32_t>(payload[1]) << 16U) |
                                   (static_cast<std::uint32_t>(payload[2]) << 8U) |
                                   static_cast<std::uint32_t>(payload[3]);
    if (protocol != static_cast<std::uint32_t>(ProtocolIdentity::kProtocol)) {
        set_error(error, "cached Login payload protocol does not match Bedrock 1.26.50/2193");
        return false;
    }
    std::size_t pos = 4U;
    std::uint32_t length = 0U;
    unsigned shift = 0U;
    bool terminated = false;
    for (unsigned i = 0U; i < 5U && pos < payload.size(); ++i) {
        const std::uint8_t byte = payload[pos++];
        if (shift == 28U && (byte & 0xf0U) != 0U) {
            set_error(error, "cached Login payload has an overflowing VarUInt32 length");
            return false;
        }
        length |= static_cast<std::uint32_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0U) { terminated = true; break; }
        shift += 7U;
    }
    if (!terminated || length == 0U || length != payload.size() - pos) {
        set_error(error, "cached Login payload has an invalid ConnectionRequest length");
        return false;
    }
    return true;
}

} // namespace

FileLoginCacheProvider::FileLoginCacheProvider(BedrockCryptoProvider& crypto, std::string cache_directory)
    : crypto_(crypto), cache_directory_(cache_directory.empty() ? default_cache_directory() : std::move(cache_directory)) {}

bool FileLoginCacheProvider::get_credentials(const LoginAuthRequestContext&,
                                             LoginCredentialMaterial&,
                                             std::string* error) {
    set_error(error, "FileLoginCacheProvider supplies a prebuilt Login artifact; use Bedrock2193SecureRuntime with this provider directly");
    return false;
}

bool FileLoginCacheProvider::get_prebuilt_login(const LoginAuthRequestContext& context,
                                                LoginArtifact& out,
                                                std::string* error) {
    out = LoginArtifact{};
    if (!context.protocol.is_target()) {
        set_error(error, "login cache requires Bedrock 1.26.50 / protocol 2193");
        return false;
    }
    if (cache_directory_.empty()) {
        set_error(error, "login cache directory is unresolved; set BEDROCK_AI_CACHE or HOME");
        return false;
    }
    if (context.server_address.empty()) {
        set_error(error, "login cache requires the exact target server host:port");
        return false;
    }

    const std::string prefix = cache_directory_ + "/";
    std::string profile;
    if (!read_text_file(prefix + "profile.json", kMaxProfileBytes, profile, error)) return false;
    std::string cached_server;
    std::string game_version;
    std::string public_key_b64;
    std::string display_name;
    std::int64_t protocol = 0;
    if (!read_json_string(profile, "server_address", cached_server) ||
        !read_json_string(profile, "game_version", game_version) ||
        !read_json_integer(profile, "protocol", protocol)) {
        set_error(error, "login cache profile is missing server_address/game_version/protocol");
        return false;
    }
    if (cached_server != context.server_address) {
        set_error(error, "login cache is signed for " + cached_server + "; rerun tools/xbox_login.py for " + context.server_address);
        return false;
    }
    if (game_version != ProtocolIdentity::kGameVersion || protocol != ProtocolIdentity::kProtocol) {
        set_error(error, "login cache profile is for a different Bedrock version/protocol");
        return false;
    }
    (void)read_json_string(profile, "identity_public_key_spki_b64", public_key_b64);
    (void)read_json_string(profile, "display_name", display_name);

    if (!read_file(prefix + "login_payload.bin", kMaxLoginPayloadBytes, out.login_packet_payload, error)) return false;
    if (!validate_login_payload(out.login_packet_payload, error)) { out = LoginArtifact{}; return false; }

    std::string pem;
    if (!read_text_file(prefix + "ecdsa_p384.pem", kMaxPemBytes, pem, error)) { out = LoginArtifact{}; return false; }
    if (!decode_pkcs8_pem(pem, out.identity_key.private_key_pkcs8_der, error)) { out = LoginArtifact{}; return false; }

    if (public_key_b64.empty() || !decode_base64(public_key_b64, out.identity_key.public_key_pkix_der)) {
        set_error(error, "login cache profile is missing a valid identity_public_key_spki_b64");
        out = LoginArtifact{};
        return false;
    }
    out.client_public_key_base64 = public_key_b64;

    // A signature/verification round trip confirms that the private and public
    // halves in the cache belong together before we start a network handshake.
    const std::vector<std::uint8_t> probe{'b','e','d','r','o','c','k','-','a','u','t','h','-','c','a','c','h','e','-','v','1'};
    std::vector<std::uint8_t> signature;
    if (!crypto_.sign_es384(out.identity_key.private_key_pkcs8_der, probe, signature, error) ||
        !crypto_.verify_es384(out.identity_key.public_key_pkix_der, probe, signature, error)) {
        set_error(error, "cached P-384 private/public key pair does not match or crypto provider is unavailable");
        out = LoginArtifact{};
        return false;
    }
    if (display_name.empty()) display_name = "xbox";
    // These fields are not used to rebuild a prebuilt request, but retaining the
    // original endpoint and account label improves diagnostics and traceability.
    out.connection_request.clear();
    set_error(error, "");
    return true;
}

} // namespace bedrockai
