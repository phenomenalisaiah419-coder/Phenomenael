#include "core/aarch64_signature.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace bedrockai {
namespace {

void set_error(std::string* error, const std::string& message) {
    if (error) *error = message;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse_byte_token(std::string_view token, std::uint8_t& value, std::uint8_t& mask) {
    if (token == "??" || token == "?") {
        value = 0;
        mask = 0;
        return true;
    }
    if (token.size() != 2U) return false;
    const int high = hex_value(token[0]);
    const int low = hex_value(token[1]);
    if (high < 0 || low < 0) return false;
    value = static_cast<std::uint8_t>((high << 4) | low);
    mask = 0xffU;
    return true;
}

} // namespace

bool SignaturePattern::parse(std::string_view text, SignaturePattern& out, std::string* error) {
    out = SignaturePattern{};
    std::istringstream stream{std::string(text)};
    std::string token;
    while (stream >> token) {
        std::uint8_t byte = 0;
        std::uint8_t mask_byte = 0;
        if (!parse_byte_token(token, byte, mask_byte)) {
            set_error(error, "invalid AArch64 signature token: " + token);
            return false;
        }
        out.bytes.push_back(byte);
        out.mask.push_back(mask_byte);
    }
    if (out.bytes.empty()) {
        set_error(error, "AArch64 signature must not be empty");
        return false;
    }
    if (out.bytes.size() != out.mask.size()) {
        set_error(error, "AArch64 signature byte/mask size mismatch");
        return false;
    }
    return true;
}

bool match_signature_at(const std::uint8_t* data,
                        std::size_t size,
                        std::size_t offset,
                        const SignaturePattern& pattern) {
    if (!data || pattern.empty() || offset > size || pattern.bytes.size() > size - offset) return false;
    for (std::size_t i = 0; i < pattern.bytes.size(); ++i) {
        if ((data[offset + i] & pattern.mask[i]) != (pattern.bytes[i] & pattern.mask[i])) return false;
    }
    return true;
}

std::vector<SignatureMatch> scan_signature(const std::uint8_t* data,
                                           std::size_t size,
                                           const SignaturePattern& pattern,
                                           std::size_t max_matches) {
    std::vector<SignatureMatch> matches;
    if (!data || pattern.empty() || pattern.bytes.size() > size || max_matches == 0U) return matches;
    const std::size_t last = size - pattern.bytes.size();
    matches.reserve(std::min(max_matches, static_cast<std::size_t>(16U)));
    for (std::size_t offset = 0; offset <= last; ++offset) {
        if (match_signature_at(data, size, offset, pattern)) {
            matches.push_back(SignatureMatch{0, 0, offset});
            if (matches.size() >= max_matches) break;
        }
    }
    return matches;
}

bool resolve_unique_signature(const std::vector<ClientExecutableSegment>& segments,
                              const SignaturePattern& pattern,
                              SignatureMatch& out,
                              std::string* error) {
    if (pattern.empty()) {
        set_error(error, "cannot resolve an empty AArch64 signature");
        return false;
    }
    std::size_t total_matches = 0;
    SignatureMatch resolved{};
    for (std::size_t segment_index = 0; segment_index < segments.size(); ++segment_index) {
        const auto& segment = segments[segment_index];
        if (segment.start == 0U || segment.size == 0U || (segment.flags & 0x1U) == 0U) continue;
        const auto* data = reinterpret_cast<const std::uint8_t*>(segment.start);
        auto matches = scan_signature(data, segment.size, pattern, 2U);
        for (const auto& local : matches) {
            if (local.offset > std::numeric_limits<std::uintptr_t>::max() - segment.start) {
                set_error(error, "AArch64 signature address overflow");
                return false;
            }
            resolved = SignatureMatch{segment.start + static_cast<std::uintptr_t>(local.offset), segment_index, local.offset};
            ++total_matches;
            if (total_matches > 1U) {
                set_error(error, "AArch64 signature is not unique in executable image");
                return false;
            }
        }
    }
    if (total_matches == 0U) {
        set_error(error, "AArch64 signature was not found in executable image");
        return false;
    }
    out = resolved;
    return true;
}

} // namespace bedrockai
