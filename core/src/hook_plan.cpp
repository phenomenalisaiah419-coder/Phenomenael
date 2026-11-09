#include "core/hook_plan.hpp"

#include <array>
#include <cctype>
#include <limits>
#include <sstream>
#include <utility>

namespace bedrockai {
namespace {

void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

std::string trim(const std::string& value) {
    std::size_t first = 0U;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) --last;
    return value.substr(first, last - first);
}

bool parse_size(const std::string& text, std::size_t& out) {
    if (text.empty()) return false;
    std::size_t value = 0U;
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c < static_cast<unsigned char>('0') || c > static_cast<unsigned char>('9')) return false;
        const std::size_t digit = static_cast<std::size_t>(c - static_cast<unsigned char>('0'));
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    out = value;
    return true;
}

bool parse_kind(const std::string& text, ClientHookKind& out) {
    if (text == "Tick") out = ClientHookKind::Tick;
    else if (text == "ReadState") out = ClientHookKind::ReadState;
    else if (text == "ReadInventory") out = ClientHookKind::ReadInventory;
    else if (text == "ReadTouch") out = ClientHookKind::ReadTouch;
    else if (text == "SubmitActions") out = ClientHookKind::SubmitActions;
    else return false;
    return true;
}

} // namespace

bool parse_client_hook_plan(const std::string& text, std::vector<ClientHookSpec>& out, std::string* error) {
    out.clear();
    std::istringstream lines(text);
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(lines, line)) {
        ++line_number;
        const std::string clean = trim(line);
        if (clean.empty() || clean[0] == '#') continue;
        std::vector<std::string> fields;
        std::size_t start = 0U;
        while (start <= clean.size()) {
            const std::size_t end = clean.find('|', start);
            fields.push_back(clean.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1U;
        }
        if (fields.size() != 7U) {
            set_error(error, "hook plan line " + std::to_string(line_number) + " must contain 7 fields");
            out.clear();
            return false;
        }
        for (auto& field : fields) field = trim(field);
        ClientHookSpec spec;
        if (!parse_kind(fields[0], spec.kind)) {
            set_error(error, "hook plan line " + std::to_string(line_number) + " has invalid hook kind");
            out.clear();
            return false;
        }
        spec.name = fields[1];
        spec.required_build_id_hex = fields[2];
        if (spec.name.empty() || spec.required_build_id_hex.empty()) {
            set_error(error, "hook plan line " + std::to_string(line_number) + " is missing name/build ID");
            out.clear();
            return false;
        }
        if (!SignaturePattern::parse(fields[3], spec.signature)) {
            set_error(error, "hook plan line " + std::to_string(line_number) + " has invalid signature");
            out.clear();
            return false;
        }
        if (!SignaturePattern::parse(fields[4], spec.expected_bytes)) {
            set_error(error, "hook plan line " + std::to_string(line_number) + " has invalid expected bytes");
            out.clear();
            return false;
        }
        if (!parse_size(fields[5], spec.patch_offset) || !parse_size(fields[6], spec.patch_size_bytes)) {
            set_error(error, "hook plan line " + std::to_string(line_number) + " has invalid numeric fields");
            out.clear();
            return false;
        }
        out.push_back(std::move(spec));
    }
    return validate_client_hook_plan(out, {}, error);
}

bool validate_client_hook_plan(const std::vector<ClientHookSpec>& specs,
                               const std::string& required_build_id_hex,
                               std::string* error) {
    if (specs.empty()) {
        set_error(error, "hook plan is empty");
        return false;
    }
    std::string build_id = required_build_id_hex;
    bool tick = false;
    for (const auto& spec : specs) {
        if (spec.name.empty()) {
            set_error(error, "hook plan contains an unnamed hook");
            return false;
        }
        if (spec.required_build_id_hex.empty()) {
            set_error(error, "hook '" + spec.name + "' has no required build ID");
            return false;
        }
        if (build_id.empty()) build_id = spec.required_build_id_hex;
        if (spec.required_build_id_hex != build_id) {
            set_error(error, "hook plan contains multiple build IDs");
            return false;
        }
        if (spec.signature.bytes.empty() || spec.signature.bytes.size() != spec.signature.mask.size()) {
            set_error(error, "hook '" + spec.name + "' has an invalid signature pattern");
            return false;
        }
        if (spec.expected_bytes.bytes.size() != spec.expected_bytes.mask.size() || spec.expected_bytes.bytes.empty()) {
            set_error(error, "hook '" + spec.name + "' has invalid expected bytes");
            return false;
        }
        if (spec.kind == ClientHookKind::Tick) {
            if (tick) {
                set_error(error, "hook plan contains multiple Tick hooks");
                return false;
            }
            tick = true;
        }
    }
    if (!tick) {
        set_error(error, "hook plan does not contain a Tick hook");
        return false;
    }
    return true;
}


bool validate_complete_client_hook_plan(const std::vector<ClientHookSpec>& specs,
                                        const std::string& required_build_id_hex,
                                        std::string* error) {
    if (!validate_client_hook_plan(specs, required_build_id_hex, error)) return false;
    std::array<bool, 5> seen{};
    for (const auto& spec : specs) {
        const auto index = static_cast<std::size_t>(spec.kind);
        if (index >= seen.size()) {
            set_error(error, "hook plan contains an unknown hook kind");
            return false;
        }
        if (seen[index]) {
            set_error(error, "hook plan contains multiple hooks of kind '" + spec.name + "'");
            return false;
        }
        seen[index] = true;
    }
    static constexpr const char* required[] = {
        "Tick", "ReadState", "ReadInventory", "ReadTouch", "SubmitActions"
    };
    for (std::size_t i = 0; i < seen.size(); ++i) {
        if (!seen[i]) {
            set_error(error, std::string("hook plan is missing required ") + required[i] + " hook");
            return false;
        }
    }
    if (specs.size() != seen.size()) {
        set_error(error, "complete hook plan must contain exactly five hooks");
        return false;
    }
    return true;
}

} // namespace bedrockai
