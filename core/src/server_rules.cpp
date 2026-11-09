#include "core/server_rules.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace bedrockai {
namespace {
constexpr std::size_t kMaxTrials = 256U;
constexpr std::uint32_t kMinConsistentEvidence = 3U;

bool valid_trial(const AttackTrial& trial) {
    return trial.interval_ticks <= 200U && std::isfinite(trial.normalized_damage) &&
           trial.normalized_damage >= 0.0 && trial.normalized_damage <= 1.0 &&
           trial.target_vulnerability_known && trial.target_vulnerable && trial.confounders_controlled;
}
}

void ServerRulesLearner::observe_attack_trial(const AttackTrial& trial) {
    if (!valid_trial(trial)) return;
    if (attack_trials_.size() >= kMaxTrials) attack_trials_.erase(attack_trials_.begin());
    attack_trials_.push_back(trial);
    recompute_attack_model();
}

void ServerRulesLearner::observe_server_family_hint(ServerFamilyHint hint, bool explicit_evidence) {
    if (!explicit_evidence || hint == ServerFamilyHint::Unknown) return;
    profile_.family_hint = hint;
}

void ServerRulesLearner::observe_feature(ServerFeature feature, bool supported, bool verified_server_result) {
    if (!verified_server_result) return;
    const auto feature_bit = 1U << static_cast<unsigned int>(feature);
    profile_.verified_feature_mask |= feature_bit;
    switch (feature) {
        case ServerFeature::AxeShieldPunish: profile_.axe_shield_punish_supported = supported; break;
        case ServerFeature::FireballUse: profile_.fireball_use_supported = supported; break;
        case ServerFeature::WindChargeClutch: profile_.wind_charge_clutch_supported = supported; break;
        case ServerFeature::BedrockScaffolding: profile_.bedrock_scaffolding_supported = supported; break;
        case ServerFeature::OffhandUse: profile_.offhand_supported = supported; break;
        case ServerFeature::CustomItemActions: profile_.custom_item_actions_supported = supported; break;
    }
}

void ServerRulesLearner::recompute_attack_model() {
    std::uint32_t fast_full = 0U;
    std::uint32_t fast_reduced = 0U;
    std::uint32_t slow_full = 0U;
    std::uint32_t slow_reduced = 0U;
    std::uint32_t min_interval = 201U;
    std::vector<std::uint32_t> full_intervals;
    for (const auto& trial : attack_trials_) {
        const bool full = trial.accepted && trial.normalized_damage >= 0.80;
        const bool reduced = !trial.accepted || trial.normalized_damage <= 0.55;
        if (full) {
            min_interval = std::min(min_interval, trial.interval_ticks);
            full_intervals.push_back(trial.interval_ticks);
        }
        if (trial.interval_ticks <= 4U) {
            if (full) ++fast_full;
            else if (reduced) ++fast_reduced;
        } else {
            if (full) ++slow_full;
            else if (reduced) ++slow_reduced;
        }
    }
    profile_.valid_trials = static_cast<std::uint32_t>(attack_trials_.size());
    profile_.minimum_observed_interval_ticks.reset();
    profile_.full_damage_interval_ticks.reset();
    if (min_interval <= 200U) profile_.minimum_observed_interval_ticks = min_interval;
    if (full_intervals.size() >= kMinConsistentEvidence) {
        std::sort(full_intervals.begin(), full_intervals.end());
        profile_.full_damage_interval_ticks = full_intervals[full_intervals.size() / 2U];
    }

    // The signal is intentionally conservative: repeated short-interval, full-damage
    // attacks support spam mechanics; repeated short-interval damped/rejected attacks
    // plus full-damage longer-interval attacks support a cooldown gate.
    if (fast_full >= kMinConsistentEvidence && fast_full >= fast_reduced + 2U) {
        profile_.attack_model = AttackModel::SpamFriendly;
        profile_.confidence = std::min(0.95, 0.55 + 0.08 * static_cast<double>(fast_full - 2U));
        profile_.evidence_note = "repeated short-interval attacks retained at least 80% normalized damage";
    } else if (fast_reduced >= kMinConsistentEvidence && slow_full >= kMinConsistentEvidence &&
               slow_full >= slow_reduced) {
        profile_.attack_model = AttackModel::CooldownGated;
        profile_.confidence = std::min(0.95, 0.55 + 0.07 * static_cast<double>(std::min(fast_reduced, slow_full) - 2U));
        profile_.evidence_note = "cooldown-gated evidence: short-interval attacks were damped/rejected while longer intervals produced full damage";
    } else if (attack_trials_.size() >= 12U && fast_full == 0U && fast_reduced == 0U) {
        profile_.attack_model = AttackModel::Custom;
        profile_.confidence = 0.40;
        profile_.evidence_note = "observations do not fit the calibrated spam/cooldown patterns; more controlled probes are needed";
    } else {
        profile_.attack_model = AttackModel::Unknown;
        profile_.confidence = attack_trials_.empty() ? 0.0 : 0.25;
        profile_.evidence_note = "not enough controlled evidence to identify the combat cadence";
    }
}

ServerMechanicsProfile ServerRulesLearner::profile() const { return profile_; }

bool ServerRulesLearner::save_tsv(const std::string& path, std::string* error) const {
    const auto set_error = [error](const std::string& message) { if (error) *error = message; };
    if (path.empty()) { set_error("server-rules path is empty"); return false; }
    const std::filesystem::path target(path), temporary(path + ".tmp");
    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) { set_error("cannot create server-rules directory: " + ec.message()); return false; }
    }
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) { set_error("cannot create server-rules temporary file"); return false; }
        out << "BEDROCKAI_SERVER_TRIALS_V1\n" << std::setprecision(17);
        out << "META\t" << static_cast<unsigned int>(profile_.family_hint) << '\t'
            << (profile_.axe_shield_punish_supported ? 1 : 0) << '\t'
            << (profile_.fireball_use_supported ? 1 : 0) << '\t'
            << (profile_.wind_charge_clutch_supported ? 1 : 0) << '\t'
            << (profile_.bedrock_scaffolding_supported ? 1 : 0) << '\t'
            << (profile_.offhand_supported ? 1 : 0) << '\t'
            << (profile_.custom_item_actions_supported ? 1 : 0) << '\t'
            << profile_.verified_feature_mask << '\n';
        for (const auto& trial : attack_trials_) {
            out << trial.interval_ticks << '\t' << trial.normalized_damage << '\t'
                << (trial.accepted ? 1 : 0) << '\t' << (trial.target_vulnerability_known ? 1 : 0) << '\t'
                << (trial.target_vulnerable ? 1 : 0) << '\t' << (trial.confounders_controlled ? 1 : 0) << '\n';
        }
        out.flush();
        if (!out) { set_error("failed writing server-rules trials"); out.close(); std::filesystem::remove(temporary, ec); return false; }
    }
    std::filesystem::permissions(temporary, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);
    if (ec) { set_error("cannot restrict server-rules permissions: " + ec.message()); std::filesystem::remove(temporary, ec); return false; }
    std::filesystem::rename(temporary, target, ec);
    if (ec) { set_error("cannot commit server-rules trials: " + ec.message()); std::error_code cleanup; std::filesystem::remove(temporary, cleanup); return false; }
    return true;
}

bool ServerRulesLearner::load_tsv(const std::string& path, std::string* error) {
    const auto set_error = [error](const std::string& message) { if (error) *error = message; };
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 128U * 1024U) { set_error(ec ? "cannot stat server-rules file" : "server-rules file exceeds size limit"); return false; }
    std::ifstream in(path);
    if (!in) { set_error("cannot open server-rules file"); return false; }
    std::string header;
    if (!std::getline(in, header) || header != "BEDROCKAI_SERVER_TRIALS_V1") {
        set_error("unsupported server-rules file header"); return false;
    }
    std::vector<AttackTrial> parsed;
    std::string line;
    bool metadata_seen = false;
    ServerMechanicsProfile metadata{};
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> cells;
        std::stringstream split(line);
        std::string cell;
        while (std::getline(split, cell, '\t')) cells.push_back(cell);
        if (!cells.empty() && cells[0] == "META") {
            if (metadata_seen || cells.size() != 9U) { set_error("invalid or duplicate server-rules metadata"); return false; }
            unsigned int family = 0U;
            std::uint32_t feature_mask = 0U;
            std::array<int, 6> features{};
            std::istringstream family_stream(cells[1]);
            std::istringstream axe_stream(cells[2]);
            std::istringstream fireball_stream(cells[3]);
            std::istringstream wind_stream(cells[4]);
            std::istringstream scaffold_stream(cells[5]);
            std::istringstream offhand_stream(cells[6]);
            std::istringstream custom_stream(cells[7]);
            std::istringstream mask_stream(cells[8]);
            std::string extra;
            if (!(family_stream >> family) || (family_stream >> extra) || family > static_cast<unsigned int>(ServerFamilyHint::CustomHybrid) ||
                !(axe_stream >> features[0]) || (axe_stream >> extra) ||
                !(fireball_stream >> features[1]) || (fireball_stream >> extra) ||
                !(wind_stream >> features[2]) || (wind_stream >> extra) ||
                !(scaffold_stream >> features[3]) || (scaffold_stream >> extra) ||
                !(offhand_stream >> features[4]) || (offhand_stream >> extra) ||
                !(custom_stream >> features[5]) || (custom_stream >> extra) ||
                !(mask_stream >> feature_mask) || (mask_stream >> extra) ||
                std::any_of(features.begin(), features.end(), [](int value) { return value != 0 && value != 1; }) ||
                (feature_mask & ~0x3fU) != 0U) {
                set_error("invalid server-rules metadata value"); return false;
            }
            metadata.family_hint = static_cast<ServerFamilyHint>(family);
            metadata.axe_shield_punish_supported = features[0] == 1;
            metadata.fireball_use_supported = features[1] == 1;
            metadata.wind_charge_clutch_supported = features[2] == 1;
            metadata.bedrock_scaffolding_supported = features[3] == 1;
            metadata.offhand_supported = features[4] == 1;
            metadata.custom_item_actions_supported = features[5] == 1;
            metadata.verified_feature_mask = feature_mask;
            metadata_seen = true;
            continue;
        }
        if (cells.size() != 6U) { set_error("server-rules trial must contain exactly six tab-separated fields"); return false; }
        AttackTrial trial;
        int accepted = -1, vulnerability_known = -1, vulnerable = -1, controlled = -1;
        std::istringstream interval_stream(cells[0]);
        std::istringstream damage_stream(cells[1]);
        std::istringstream accepted_stream(cells[2]);
        std::istringstream known_stream(cells[3]);
        std::istringstream vulnerable_stream(cells[4]);
        std::istringstream controlled_stream(cells[5]);
        std::string extra;
        if (!(interval_stream >> trial.interval_ticks) || (interval_stream >> extra) ||
            !(damage_stream >> trial.normalized_damage) || (damage_stream >> extra) ||
            !(accepted_stream >> accepted) || (accepted_stream >> extra) ||
            !(known_stream >> vulnerability_known) || (known_stream >> extra) ||
            !(vulnerable_stream >> vulnerable) || (vulnerable_stream >> extra) ||
            !(controlled_stream >> controlled) || (controlled_stream >> extra) ||
            accepted < 0 || accepted > 1 || vulnerability_known < 0 || vulnerability_known > 1 ||
            vulnerable < 0 || vulnerable > 1 || controlled < 0 || controlled > 1) {
            set_error("invalid value in server-rules trial row"); return false;
        }
        trial.accepted = accepted == 1;
        trial.target_vulnerability_known = vulnerability_known == 1;
        trial.target_vulnerable = vulnerable == 1;
        trial.confounders_controlled = controlled == 1;
        if (!valid_trial(trial)) { set_error("server-rules trial violates evidence bounds"); return false; }
        parsed.push_back(trial);
        if (parsed.size() > kMaxTrials) { set_error("server-rules file has too many trials"); return false; }
    }
    if (!in.eof()) { set_error("failed reading server-rules data"); return false; }
    attack_trials_.clear();
    profile_ = {};
    for (const auto& trial : parsed) attack_trials_.push_back(trial);
    recompute_attack_model();
    if (metadata_seen) {
        profile_.family_hint = metadata.family_hint;
        profile_.axe_shield_punish_supported = metadata.axe_shield_punish_supported;
        profile_.fireball_use_supported = metadata.fireball_use_supported;
        profile_.wind_charge_clutch_supported = metadata.wind_charge_clutch_supported;
        profile_.bedrock_scaffolding_supported = metadata.bedrock_scaffolding_supported;
        profile_.offhand_supported = metadata.offhand_supported;
        profile_.custom_item_actions_supported = metadata.custom_item_actions_supported;
        profile_.verified_feature_mask = metadata.verified_feature_mask;
    }
    return true;
}

void ServerRulesLearner::reset() {
    attack_trials_.clear();
    profile_ = {};
}

} // namespace bedrockai
