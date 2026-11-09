#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bedrockai {

enum class AttackModel : std::uint8_t {
    Unknown,
    SpamFriendly,
    CooldownGated,
    Custom
};

enum class ServerFamilyHint : std::uint8_t {
    Unknown,
    NativeBedrock,
    JavaViaBedrockProxy,
    CustomHybrid
};

enum class ServerFeature : std::uint8_t {
    AxeShieldPunish,
    FireballUse,
    WindChargeClutch,
    BedrockScaffolding,
    OffhandUse,
    CustomItemActions
};

struct AttackTrial {
    // Tick interval between the prior attack attempt and this attempt.
    std::uint32_t interval_ticks = 0;
    // Observed health loss divided by expected unarmoured damage (clamped to [0,1]).
    // Supply only when target vulnerability and other damage modifiers are known.
    double normalized_damage = 0.0;
    bool accepted = false;
    bool target_vulnerability_known = false;
    bool target_vulnerable = false;
    bool confounders_controlled = false;
};

struct ServerMechanicsProfile {
    AttackModel attack_model = AttackModel::Unknown;
    ServerFamilyHint family_hint = ServerFamilyHint::Unknown;
    double confidence = 0.0;
    std::uint32_t valid_trials = 0;
    std::optional<std::uint32_t> full_damage_interval_ticks;
    std::optional<std::uint32_t> minimum_observed_interval_ticks;
    bool axe_shield_punish_supported = false;
    bool fireball_use_supported = false;
    bool wind_charge_clutch_supported = false;
    bool bedrock_scaffolding_supported = false;
    bool offhand_supported = false;
    bool custom_item_actions_supported = false;
    std::uint32_t verified_feature_mask = 0U;
    std::string evidence_note;
};

// Evidence-driven per-server rules learner. It deliberately does not infer a server's
// mechanics merely from the client edition or a server's "cracked" label.
class ServerRulesLearner {
public:
    void observe_attack_trial(const AttackTrial& trial);
    void observe_server_family_hint(ServerFamilyHint hint, bool explicit_evidence);
    void observe_feature(ServerFeature feature, bool supported, bool verified_server_result);
    ServerMechanicsProfile profile() const;
    bool save_tsv(const std::string& path, std::string* error = nullptr) const;
    bool load_tsv(const std::string& path, std::string* error = nullptr);
    void reset();

private:
    std::vector<AttackTrial> attack_trials_;
    ServerMechanicsProfile profile_{};
    void recompute_attack_model();
};

} // namespace bedrockai
