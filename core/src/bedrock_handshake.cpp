#include "core/bedrock_handshake.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>

namespace bedrockai {
namespace {

void set_error(std::string* error, const std::string& message) {
    if (error) *error = message;
}

int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + (c - 'a');
    if (c >= '0' && c <= '9') return 52 + (c - '0');
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

bool decode_b64(const std::string& input, bool urlsafe, std::vector<std::uint8_t>& output, std::string* error) {
    if (input.empty()) {
        output.clear();
        return true;
    }

    std::uint32_t acc = 0;
    unsigned bits = 0;
    bool seen_padding = false;
    output.clear();
    output.reserve((input.size() * 3) / 4 + 2);

    for (char c : input) {
        if (c == '=') {
            seen_padding = true;
            continue;
        }
        if (seen_padding) {
            set_error(error, "base64 padding must terminate the value");
            return false;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            set_error(error, "base64 value must not contain whitespace");
            return false;
        }
        if (!urlsafe && (c == '-' || c == '_')) {
            set_error(error, "URL-safe base64 character in standard base64 value");
            return false;
        }
        if (urlsafe && (c == '+' || c == '/')) {
            set_error(error, "standard base64 character in URL-safe base64 value");
            return false;
        }
        const int value = b64_value(c);
        if (value < 0) {
            set_error(error, "invalid base64 character");
            return false;
        }
        acc = (acc << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xffu));
        }
    }

    if (bits >= 6 || (bits != 0 && (acc & ((1u << bits) - 1u)) != 0)) {
        set_error(error, "non-canonical base64 trailing bits");
        output.clear();
        return false;
    }
    return true;
}

bool json_extract_string(const std::string& json, const std::string& field,
                         std::string& value, std::string* error) {
    const std::string needle = "\"" + field + "\"";
    std::size_t search = 0;
    while ((search = json.find(needle, search)) != std::string::npos) {
        std::size_t p = search + needle.size();
        while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])) != 0) ++p;
        if (p >= json.size() || json[p] != ':') {
            search += needle.size();
            continue;
        }
        ++p;
        while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p])) != 0) ++p;
        if (p >= json.size() || json[p] != '"') {
            set_error(error, "JSON field '" + field + "' is not a string");
            return false;
        }
        ++p;
        value.clear();
        while (p < json.size()) {
            const char c = json[p++];
            if (c == '"') return true;
            if (c != '\\') {
                if (static_cast<unsigned char>(c) < 0x20u) {
                    set_error(error, "control character in JSON string");
                    return false;
                }
                value.push_back(c);
                continue;
            }
            if (p >= json.size()) {
                set_error(error, "unterminated JSON string escape");
                return false;
            }
            const char esc = json[p++];
            switch (esc) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default:
                    set_error(error, "unsupported JSON escape in field '" + field + "'");
                    return false;
            }
        }
        set_error(error, "unterminated JSON string field '" + field + "'");
        return false;
    }
    set_error(error, "JSON field '" + field + "' not found");
    return false;
}

} // namespace

bool BedrockServerToClientHandshakeProcessor::base64_decode_flexible(
    const std::string& input, std::vector<std::uint8_t>& output, std::string* error) {
    // The Bedrock handshake currently carries salt and x5u using standard base64,
    // but accepting URL-safe base64 here is harmless and keeps the parser robust to
    // hosts that normalize the token representation.
    bool has_url = false;
    bool has_standard = false;
    for (char c : input) {
        has_url = has_url || c == '-' || c == '_';
        has_standard = has_standard || c == '+' || c == '/';
    }
    if (has_url && has_standard) {
        set_error(error, "mixed base64 alphabets are not accepted");
        return false;
    }
    return decode_b64(input, has_url, output, error);
}

bool BedrockServerToClientHandshakeProcessor::base64url_decode(
    const std::string& input, std::vector<std::uint8_t>& output, std::string* error) {
    return decode_b64(input, true, output, error);
}

bool BedrockServerToClientHandshakeProcessor::extract_json_string_field(
    const std::string& json, const std::string& field, std::string& value, std::string* error) {
    return json_extract_string(json, field, value, error);
}

bool BedrockServerToClientHandshakeProcessor::split_compact_jwt(
    const std::string& jwt, std::string& header_b64, std::string& payload_b64,
    std::string& signature_b64, std::string* error) {
    const std::size_t p1 = jwt.find('.');
    const std::size_t p2 = p1 == std::string::npos ? std::string::npos : jwt.find('.', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos || jwt.find('.', p2 + 1) != std::string::npos ||
        p1 == 0 || p2 == p1 + 1 || p2 + 1 >= jwt.size()) {
        set_error(error, "ServerToClientHandshake must contain exactly three non-empty JWT segments");
        return false;
    }
    header_b64 = jwt.substr(0, p1);
    payload_b64 = jwt.substr(p1 + 1, p2 - p1 - 1);
    signature_b64 = jwt.substr(p2 + 1);
    return true;
}

bool BedrockServerToClientHandshakeProcessor::process(
    const std::vector<std::uint8_t>& jwt,
    const std::vector<std::uint8_t>& client_private_key_pkcs8_der,
    ServerEncryptionHandshake& out,
    std::string* error) const {
    out = ServerEncryptionHandshake{};
    constexpr std::size_t kMaxJwtBytes = 64 * 1024;
    if (jwt.empty() || jwt.size() > kMaxJwtBytes) {
        set_error(error, "ServerToClientHandshake JWT length is invalid");
        return false;
    }

    const std::string token(jwt.begin(), jwt.end());
    std::string header_b64;
    std::string payload_b64;
    std::string signature_b64;
    if (!split_compact_jwt(token, header_b64, payload_b64, signature_b64, error)) return false;

    std::vector<std::uint8_t> header_bytes;
    std::vector<std::uint8_t> payload_bytes;
    std::vector<std::uint8_t> signature;
    if (!base64url_decode(header_b64, header_bytes, error) ||
        !base64url_decode(payload_b64, payload_bytes, error) ||
        !base64url_decode(signature_b64, signature, error)) {
        return false;
    }
    if (signature.size() != 96) {
        set_error(error, "ServerToClientHandshake ES384 signature must be 96 bytes");
        return false;
    }

    const std::string header(header_bytes.begin(), header_bytes.end());
    const std::string payload(payload_bytes.begin(), payload_bytes.end());
    std::string algorithm;
    std::string x5u;
    std::string salt_b64;
    if (!extract_json_string_field(header, "alg", algorithm, error) ||
        !extract_json_string_field(header, "x5u", x5u, error) ||
        !extract_json_string_field(payload, "salt", salt_b64, error)) {
        return false;
    }
    if (algorithm != "ES384") {
        set_error(error, "ServerToClientHandshake JWT algorithm must be ES384");
        return false;
    }
    if (x5u.empty() || x5u.size() > 4096) {
        set_error(error, "ServerToClientHandshake x5u is invalid");
        return false;
    }

    if (!base64_decode_flexible(x5u, out.server_public_key_pkix_der, error) ||
        out.server_public_key_pkix_der.empty()) {
        if (error && error->empty()) *error = "failed to decode server x5u public key";
        return false;
    }
    if (!base64_decode_flexible(salt_b64, out.salt, error) || out.salt.size() != 16) {
        if (error && error->empty()) *error = "ServerToClientHandshake salt must decode to exactly 16 bytes";
        return false;
    }

    const std::vector<std::uint8_t> signing_input(token.begin(), token.begin() + static_cast<std::ptrdiff_t>(token.find('.', token.find('.') + 1)));
    if (!crypto_.verify_es384(out.server_public_key_pkix_der, signing_input, signature, error)) return false;

    std::vector<std::uint8_t> shared_secret_x;
    if (!crypto_.p384_shared_secret_x(client_private_key_pkcs8_der,
                                      out.server_public_key_pkix_der,
                                      shared_secret_x, error)) {
        return false;
    }
    if (shared_secret_x.size() != 48) {
        set_error(error, "P-384 shared secret must be exactly 48 bytes");
        return false;
    }

    std::vector<std::uint8_t> key_material;
    key_material.reserve(out.salt.size() + shared_secret_x.size());
    key_material.insert(key_material.end(), out.salt.begin(), out.salt.end());
    key_material.insert(key_material.end(), shared_secret_x.begin(), shared_secret_x.end());
    if (!crypto_.sha256(key_material, out.encryption_key, error)) return false;
    return true;
}

} // namespace bedrockai
