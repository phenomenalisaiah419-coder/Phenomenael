#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bedrockai {

struct ClientExecutableSegment {
    std::uintptr_t start = 0;
    std::size_t size = 0;
    std::uint32_t flags = 0;
};

struct SignaturePattern {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> mask;

    static bool parse(std::string_view text, SignaturePattern& out, std::string* error = nullptr);
    bool empty() const { return bytes.empty(); }
};

struct SignatureMatch {
    std::uintptr_t address = 0;
    std::size_t segment_index = 0;
    std::size_t offset = 0;
};

std::vector<SignatureMatch> scan_signature(const std::uint8_t* data,
                                           std::size_t size,
                                           const SignaturePattern& pattern,
                                           std::size_t max_matches = 64);

bool resolve_unique_signature(const std::vector<ClientExecutableSegment>& segments,
                              const SignaturePattern& pattern,
                              SignatureMatch& out,
                              std::string* error = nullptr);

bool match_signature_at(const std::uint8_t* data,
                        std::size_t size,
                        std::size_t offset,
                        const SignaturePattern& pattern);

} // namespace bedrockai
