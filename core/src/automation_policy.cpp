#include "core/automation_policy.hpp"

#include <algorithm>

namespace bedrockai {

const char* assist_level_name(AssistLevel level) {
    switch (level) {
        case AssistLevel::Off: return "off";
        case AssistLevel::ObserveOnly: return "observe_only";
        case AssistLevel::SurvivalOnly: return "survival_only";
        case AssistLevel::Assist: return "assist";
        case AssistLevel::Takeover: return "takeover";
    }
    return "off";
}

const char* server_class_name(ServerClass cls) {
    switch (cls) {
        case ServerClass::Unknown: return "unknown";
        case ServerClass::OwnWorld: return "own_world";
        case ServerClass::PrivateRealm: return "private_realm";
        case ServerClass::PracticeServer: return "practice_server";
        case ServerClass::PublicCompetitive: return "public_competitive";
        case ServerClass::PublicSocial: return "public_social";
    }
    return "unknown";
}

AssistLevel AutomationPolicy::default_ceiling(ServerClass cls) {
    switch (cls) {
        case ServerClass::OwnWorld:
        case ServerClass::PrivateRealm: return AssistLevel::Takeover;
        case ServerClass::PracticeServer: return AssistLevel::Assist;
        case ServerClass::PublicCompetitive:
        case ServerClass::PublicSocial:
        case ServerClass::Unknown: return AssistLevel::ObserveOnly;
    }
    return AssistLevel::ObserveOnly;
}

bool AutomationPolicy::grant(const AutomationGrant& g, std::string* error) {
    auto fail = [&](const char* m) {
        if (error) *error = m;
        return false;
    };
    if (g.server_key.empty()) return fail("grant needs a server key");
    if (!g.user_confirmed_rules_allow) return fail("grant requires the user to confirm the server rules allow automation");
    if (g.max_level > AssistLevel::Takeover) return fail("grant level out of range");
    if (grants_.size() >= 256U && grants_.find(g.server_key) == grants_.end()) return fail("too many grants");
    grants_[g.server_key] = g.max_level;
    return true;
}

AssistLevel AutomationPolicy::ceiling(const std::string& server_key, ServerClass cls) const {
    const AssistLevel base = default_ceiling(cls);
    const auto it = grants_.find(server_key);
    if (it == grants_.end()) return base;
    return std::max(base, it->second);
}

AssistLevel AutomationPolicy::effective(const std::string& server_key, ServerClass cls, AssistLevel requested) const {
    return std::min(requested, ceiling(server_key, cls));
}

UserMode AutomationPolicy::to_user_mode(AssistLevel level) {
    switch (level) {
        case AssistLevel::Off:
        case AssistLevel::ObserveOnly: return UserMode::Off;
        case AssistLevel::SurvivalOnly: return UserMode::GuardOnly;
        case AssistLevel::Assist: return UserMode::Assist;
        case AssistLevel::Takeover: return UserMode::Takeover;
    }
    return UserMode::Off;
}

EditionProfile bedrock_profile(AuthMode auth) {
    EditionProfile p;
    p.edition = GameEdition::Bedrock;
    p.auth = auth;
    p.label = "bedrock-1.26.50-protocol-2193";
    p.attack_cooldown_hint = false;
    p.block_hitting_hint = false;
    p.touch_controls = true;
    p.wire = WireStatus::Partial;
    p.needs_login_cache = auth == AuthMode::XboxLive;
    p.notes = auth == AuthMode::OfflineCracked
                  ? "Offline login only works on servers started with online-mode=false; the self-signed login path is NOT implemented yet."
                  : "RakNet/NetherNet transport and secure login are implemented as libraries; live same-character control is not proven.";
    return p;
}

EditionProfile java_profile(AuthMode auth) {
    EditionProfile p;
    p.edition = GameEdition::Java;
    p.auth = auth;
    p.label = "java-adapter-pending";
    p.attack_cooldown_hint = true;
    p.block_hitting_hint = true;
    p.touch_controls = false;
    p.wire = WireStatus::NotImplemented;
    p.needs_login_cache = auth == AuthMode::JavaMicrosoft;
    p.notes = "Java wire protocol is NOT implemented. Decision logic, learning and the technique catalogue are edition-aware and reusable; "
              "a Java adapter must supply observations and execute the typed actions.";
    return p;
}

bool edition_matches(std::uint8_t edition_mask, GameEdition edition) {
    const std::uint8_t bit = edition == GameEdition::Bedrock ? 1U : 2U;
    return (edition_mask & bit) != 0U;
}

} // namespace bedrockai
