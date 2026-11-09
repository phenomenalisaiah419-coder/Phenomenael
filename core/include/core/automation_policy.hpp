#pragma once

// Where the AI is allowed to act (v0.35), and which game edition it is playing (v0.35).
//
// Fair-play gate.  Automation on a public competitive server is normally against that server's
// rules and gets accounts banned.  The controller therefore never assumes permission:
//   * Unknown / public servers default to ObserveOnly (it may watch and learn, never act).
//   * Your own world and private realms default to full Takeover.
//   * Practice servers default to Assist.
//   * Raising a ceiling needs an explicit grant that records "the user confirmed the server's
//     rules allow this".  A grant can never exceed Takeover and is per server key.
// There is no anti-cheat bypass, spoofing or evasion anywhere in this code base.

#include "core/takeover_authority.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace bedrockai {

enum class AssistLevel : std::uint8_t { Off = 0, ObserveOnly, SurvivalOnly, Assist, Takeover };
enum class ServerClass : std::uint8_t { Unknown, OwnWorld, PrivateRealm, PracticeServer, PublicCompetitive, PublicSocial };

const char* assist_level_name(AssistLevel level);
const char* server_class_name(ServerClass cls);

struct AutomationGrant {
    std::string server_key;
    AssistLevel max_level = AssistLevel::ObserveOnly;
    bool user_confirmed_rules_allow = false;
};

class AutomationPolicy {
public:
    static AssistLevel default_ceiling(ServerClass cls);
    bool grant(const AutomationGrant& grant, std::string* error = nullptr);
    void revoke(const std::string& server_key) { grants_.erase(server_key); }
    AssistLevel ceiling(const std::string& server_key, ServerClass cls) const;
    AssistLevel effective(const std::string& server_key, ServerClass cls, AssistLevel requested) const;
    bool automation_permitted(const std::string& server_key, ServerClass cls) const {
        return ceiling(server_key, cls) >= AssistLevel::SurvivalOnly;
    }
    static UserMode to_user_mode(AssistLevel level);

private:
    std::unordered_map<std::string, AssistLevel> grants_;
};

// ---------------------------------------------------------------- editions

enum class GameEdition : std::uint8_t { Bedrock, Java };
enum class AuthMode : std::uint8_t { XboxLive, JavaMicrosoft, OfflineCracked };
enum class WireStatus : std::uint8_t { Partial, NotImplemented };

struct EditionProfile {
    GameEdition edition = GameEdition::Bedrock;
    AuthMode auth = AuthMode::XboxLive;
    std::string label;
    // Mechanics HINTS used only to pick candidate techniques.  The truth is always measured
    // (ServerRulesLearner / MechanicsProbe) and a hint never enables an action by itself.
    bool attack_cooldown_hint = false;
    bool block_hitting_hint = false;
    bool touch_controls = false;
    WireStatus wire = WireStatus::Partial;
    bool needs_login_cache = true;
    std::string notes;
};

EditionProfile bedrock_profile(AuthMode auth = AuthMode::XboxLive);
EditionProfile java_profile(AuthMode auth = AuthMode::JavaMicrosoft);
bool edition_matches(std::uint8_t edition_mask, GameEdition edition);

} // namespace bedrockai
