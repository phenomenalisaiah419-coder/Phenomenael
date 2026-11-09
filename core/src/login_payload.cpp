#include "core/login_payload.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <sstream>

namespace bedrockai {
namespace {

void set_error(std::string* error, const char* message) { if (error) *error = message; }
void put_le32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) { out.push_back(static_cast<std::uint8_t>(value & 0xffu)); value >>= 8; }
}
void put_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 3; i >= 0; --i) out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xffu));
}
std::string now_seconds_json() {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(static_cast<long long>(now));
}

} // namespace

bool StaticLoginAuthProvider::get_credentials(const LoginAuthRequestContext& context,
                                               LoginCredentialMaterial& out,
                                               std::string* error) {
    if (context.protocol.protocol != ProtocolIdentity::kProtocol || context.protocol.game_version != ProtocolIdentity::kGameVersion) {
        set_error(error, "auth provider received an unsupported Bedrock identity"); return false;
    }
    if (material_.login_chain_json.empty()) { set_error(error, "login chain JSON is empty"); return false; }
    if (material_.client_data_json.empty()) { set_error(error, "ClientData JSON is empty"); return false; }
    if (material_.authentication_type != 0 && material_.authentication_type != 2) {
        set_error(error, "unsupported authentication type"); return false;
    }
    out = material_;
    return true;
}

bool CallbackLoginAuthProvider::get_credentials(const LoginAuthRequestContext& context,
                                                 LoginCredentialMaterial& out,
                                                 std::string* error) {
    if (!callback_) {
        if (error) *error = "authentication callback is not configured";
        return false;
    }
    if (!context.protocol.is_target()) {
        if (error) *error = "authentication callback received an unsupported Bedrock identity";
        return false;
    }
    return callback_(context, out, error);
}

std::vector<std::uint8_t> Bedrock2193LoginPayloadBuilder::base64url_decode(const std::string& input, bool* ok) {
    std::vector<std::uint8_t> out;
    std::uint32_t acc = 0;
    unsigned bits = 0;
    for (char c : input) {
        std::uint8_t v;
        if (c >= 'A' && c <= 'Z') v = static_cast<std::uint8_t>(c - 'A');
        else if (c >= 'a' && c <= 'z') v = static_cast<std::uint8_t>(26 + c - 'a');
        else if (c >= '0' && c <= '9') v = static_cast<std::uint8_t>(52 + c - '0');
        else if (c == '-') v = 62;
        else if (c == '_') v = 63;
        else { if (ok) *ok = false; return {}; }
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xffu)); }
    }
    if (bits >= 6 || (bits && (acc & ((1u << bits) - 1u)) != 0)) { if (ok) *ok = false; return {}; }
    if (ok) *ok = true;
    return out;
}

std::string Bedrock2193LoginPayloadBuilder::base64url_encode(const std::vector<std::uint8_t>& input) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((input.size() * 4 + 2) / 3);
    std::uint32_t acc = 0; unsigned bits = 0;
    for (auto b : input) {
        acc = (acc << 8) | b; bits += 8;
        while (bits >= 6) { bits -= 6; out.push_back(table[(acc >> bits) & 0x3f]); }
    }
    if (bits) out.push_back(table[(acc << (6 - bits)) & 0x3f]);
    return out;
}

bool Bedrock2193LoginPayloadBuilder::json_string_escape(const std::string& input, std::string& out) {
    out.clear(); out.reserve(input.size() + 8);
    for (const char raw : input) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 0xf]);
                } else out.push_back(static_cast<char>(c));
        }
    }
    return true;
}

bool Bedrock2193LoginPayloadBuilder::extract_json_string_field(const std::string& json,
                                                                const std::string& field,
                                                                std::string& value) {
    const std::string needle = "\"" + field + "\"";
    std::size_t pos = json.find(needle);
    while (pos != std::string::npos) {
        pos += needle.size();
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
        if (pos >= json.size() || json[pos] != ':') { pos = json.find(needle, pos); continue; }
        ++pos;
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
        if (pos >= json.size() || json[pos] != '"') return false;
        ++pos;
        value.clear();
        while (pos < json.size()) {
            char c = json[pos++];
            if (c == '"') return true;
            if (c != '\\') { value.push_back(c); continue; }
            if (pos >= json.size()) return false;
            c = json[pos++];
            switch (c) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: return false;
            }
        }
        return false;
    }
    return false;
}

bool Bedrock2193LoginPayloadBuilder::extract_first_chain_token(const std::string& chain_json,
                                                                std::string& token,
                                                                std::string* error) {
    const std::size_t chain_key = chain_json.find("\"chain\"");
    if (chain_key == std::string::npos) { set_error(error, "login chain JSON has no chain array"); return false; }
    const std::size_t open = chain_json.find('[', chain_key);
    if (open == std::string::npos) { set_error(error, "login chain JSON has no chain array opener"); return false; }
    std::size_t p = open + 1;
    while (p < chain_json.size() && std::isspace(static_cast<unsigned char>(chain_json[p]))) ++p;
    if (p >= chain_json.size() || chain_json[p] != '"') { set_error(error, "login chain first element is not a string"); return false; }
    ++p; token.clear();
    while (p < chain_json.size()) {
        char c = chain_json[p++];
        if (c == '"') break;
        if (c == '\\') {
            if (p >= chain_json.size()) { set_error(error, "unterminated login chain token"); return false; }
            char e = chain_json[p++];
            if (e != '"' && e != '\\' && e != '/') { set_error(error, "unsupported escape in login chain token"); return false; }
            token.push_back(e);
        } else token.push_back(c);
    }
    if (token.empty()) { set_error(error, "login chain first token is empty"); return false; }
    return true;
}

bool Bedrock2193LoginPayloadBuilder::sign_es384_compact_jwt(const std::string& header_json,
                                                             const std::string& payload_json,
                                                             const EcKeyPairDer& key,
                                                             std::string& jwt,
                                                             std::string* error) {
    std::vector<std::uint8_t> header(header_json.begin(), header_json.end());
    std::vector<std::uint8_t> payload(payload_json.begin(), payload_json.end());
    const std::string encoded_header = base64url_encode(header);
    const std::string encoded_payload = base64url_encode(payload);
    const std::string signing = encoded_header + "." + encoded_payload;
    std::vector<std::uint8_t> signature;
    if (!crypto_.sign_es384(key.private_key_pkcs8_der,
                            std::vector<std::uint8_t>(signing.begin(), signing.end()),
                            signature, error)) return false;
    if (signature.size() != 96) { set_error(error, "ES384 provider returned an invalid JOSE signature size"); return false; }
    jwt = signing + "." + base64url_encode(signature);
    return true;
}

bool Bedrock2193LoginPayloadBuilder::validate_multiplayer_token_public_key(
    const LoginCredentialMaterial& material,
    const std::string& expected_public_key_b64,
    std::string* error) {
    if (material.authentication_type == 2) return true;
    if (material.multiplayer_token.empty()) { set_error(error, "modern authentication requires a multiplayer token"); return false; }
    const std::size_t p1 = material.multiplayer_token.find('.');
    if (p1 == std::string::npos) { set_error(error, "multiplayer token is not compact JWT"); return false; }
    const std::size_t p2 = material.multiplayer_token.find('.', p1 + 1);
    if (p2 == std::string::npos) { set_error(error, "multiplayer token is not compact JWT"); return false; }
    bool ok = false;
    const auto payload = base64url_decode(material.multiplayer_token.substr(p1 + 1, p2 - p1 - 1), &ok);
    if (!ok) { set_error(error, "multiplayer token payload is not valid base64url"); return false; }
    const std::string payload_json(payload.begin(), payload.end());
    std::string cpk;
    if (!extract_json_string_field(payload_json, "cpk", cpk) || cpk != expected_public_key_b64) {
        set_error(error, "multiplayer token cpk does not match generated client public key"); return false;
    }
    return true;
}

bool Bedrock2193LoginPayloadBuilder::build(const LoginAuthRequestContext& context,
                                            BedrockLoginAuthProvider& auth,
                                            LoginArtifact& out,
                                            std::string* error) {
    if (context.protocol.game_version != ProtocolIdentity::kGameVersion || context.protocol.protocol != ProtocolIdentity::kProtocol) {
        set_error(error, "login builder requires Bedrock 1.26.50 / protocol 2193"); return false;
    }
    out = LoginArtifact{};
    if (!crypto_.generate_p384_keypair(out.identity_key, error)) return false;
    out.client_public_key_base64 = base64url_encode(out.identity_key.public_key_pkix_der);
    // Auth services expect standard Base64 for cpk/x5u, not the JWT base64url alphabet.
    // Use a small standard Base64 encoder here.
    {
        static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string b64;
        const auto& input = out.identity_key.public_key_pkix_der;
        std::uint32_t acc = 0; unsigned bits = 0;
        for (auto b : input) {
            acc = (acc << 8) | b; bits += 8;
            while (bits >= 6) { bits -= 6; b64.push_back(table[(acc >> bits) & 0x3f]); }
        }
        if (bits) b64.push_back(table[(acc << (6 - bits)) & 0x3f]);
        while (b64.size() % 4) b64.push_back('=');
        out.client_public_key_base64 = std::move(b64);
    }

    LoginAuthRequestContext request = context;
    request.client_public_key_base64 = out.client_public_key_base64;
    LoginCredentialMaterial material;
    if (!auth.get_credentials(request, material, error)) return false;
    if (!validate_multiplayer_token_public_key(material, out.client_public_key_base64, error)) return false;
    if (material.client_data_json.empty() || material.client_data_json.front() != '{') {
        set_error(error, "ClientData JSON must be an object"); return false;
    }

    std::string first_chain_token;
    if (!extract_first_chain_token(material.login_chain_json, first_chain_token, error)) return false;
    const std::size_t dot = first_chain_token.find('.');
    if (dot == std::string::npos) { set_error(error, "first login chain token is not compact JWT"); return false; }
    bool ok = false;
    const auto header_bytes = base64url_decode(first_chain_token.substr(0, dot), &ok);
    if (!ok) { set_error(error, "first login chain JWT header is invalid base64url"); return false; }
    const std::string header_json(header_bytes.begin(), header_bytes.end());
    std::string previous_x5u;
    if (!extract_json_string_field(header_json, "x5u", previous_x5u) || previous_x5u.empty()) {
        set_error(error, "first login chain JWT has no x5u header"); return false;
    }

    const std::string now = now_seconds_json();
    const std::string identity_payload =
        std::string("{\"exp\":") + now + ",\"nbf\":" + std::to_string(std::stoll(now) - 21600) +
        ",\"identityPublicKey\":\"" + previous_x5u + "\",\"certificateAuthority\":true}";
    std::string signed_identity;
    if (!sign_es384_compact_jwt(
            std::string("{\"alg\":\"ES384\",\"x5u\":\"") + out.client_public_key_base64 + "\"}",
            identity_payload, out.identity_key, signed_identity, error)) return false;

    const std::size_t chain_key = material.login_chain_json.find("\"chain\"");
    if (chain_key == std::string::npos) { set_error(error, "login chain JSON has no chain member"); return false; }
    const std::size_t colon = material.login_chain_json.find(':', chain_key + 7);
    if (colon == std::string::npos) { set_error(error, "login chain JSON chain member has no colon"); return false; }
    std::size_t chain_array = colon + 1;
    while (chain_array < material.login_chain_json.size() && std::isspace(static_cast<unsigned char>(material.login_chain_json[chain_array]))) ++chain_array;
    if (chain_array >= material.login_chain_json.size() || material.login_chain_json[chain_array] != '[') {
        set_error(error, "login chain member is not an array"); return false;
    }
    std::string chain_escaped;
    std::string certificate_json = material.login_chain_json.substr(0, chain_array + 1);
    certificate_json += "\"" + signed_identity + "\",";
    certificate_json += material.login_chain_json.substr(chain_array + 1);

    std::string certificate_escaped;
    json_string_escape(certificate_json, certificate_escaped);
    std::string token_escaped;
    json_string_escape(material.multiplayer_token, token_escaped);
    std::string client_header = std::string("{\"alg\":\"ES384\",\"x5u\":\"") + out.client_public_key_base64 + "\"}";
    std::string client_token;
    if (!sign_es384_compact_jwt(client_header, material.client_data_json, out.identity_key, client_token, error)) return false;

    std::string request_json = std::string("{\"Certificate\":\"") + certificate_escaped +
                               "\",\"AuthenticationType\":" + std::to_string(material.authentication_type) +
                               ",\"Token\":\"" + token_escaped + "\"}";
    constexpr std::size_t kMaxI32 = static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
    if (request_json.size() > kMaxI32 || client_token.size() > kMaxI32) {
        set_error(error, "Login connection request field exceeds signed 32-bit length"); return false;
    }
    out.connection_request.clear();
    put_le32(out.connection_request, static_cast<std::uint32_t>(request_json.size()));
    out.connection_request.insert(out.connection_request.end(), request_json.begin(), request_json.end());
    put_le32(out.connection_request, static_cast<std::uint32_t>(client_token.size()));
    out.connection_request.insert(out.connection_request.end(), client_token.begin(), client_token.end());

    if (!encode_login_packet_payload(static_cast<std::int32_t>(ProtocolIdentity::kProtocol), out.connection_request,
                                     out.login_packet_payload, error)) return false;
    return true;
}

bool Bedrock2193LoginPayloadBuilder::encode_login_packet_payload(
    std::int32_t protocol_version,
    const std::vector<std::uint8_t>& connection_request,
    std::vector<std::uint8_t>& out,
    std::string* error) {
    if (protocol_version <= 0) { set_error(error, "protocol version must be positive"); return false; }
    if (connection_request.empty()) { set_error(error, "connection request is empty"); return false; }
    if (connection_request.size() > std::numeric_limits<std::uint32_t>::max()) { set_error(error, "connection request is too large"); return false; }
    out.clear(); out.reserve(4 + 5 + connection_request.size());
    put_be32(out, static_cast<std::uint32_t>(protocol_version));
    std::uint32_t len = static_cast<std::uint32_t>(connection_request.size());
    do { std::uint8_t b = static_cast<std::uint8_t>(len & 0x7f); len >>= 7; if (len) b |= 0x80; out.push_back(b); } while (len);
    out.insert(out.end(), connection_request.begin(), connection_request.end());
    return true;
}

} // namespace bedrockai
