#pragma once

#include "hook_binding.hpp"

#include <string>
#include <vector>

namespace bedrockai {

bool parse_client_hook_plan(const std::string& text,
                            std::vector<ClientHookSpec>& out,
                            std::string* error = nullptr);

bool validate_client_hook_plan(const std::vector<ClientHookSpec>& specs,
                               const std::string& required_build_id_hex = {},
                               std::string* error = nullptr);

bool validate_complete_client_hook_plan(const std::vector<ClientHookSpec>& specs,
                                        const std::string& required_build_id_hex = {},
                                        std::string* error = nullptr);

} // namespace bedrockai
