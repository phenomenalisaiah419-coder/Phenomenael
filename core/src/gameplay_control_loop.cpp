#include "core/gameplay_control_loop.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace bedrockai {
namespace {
std::string stable_profile_hash(const std::string& value) {
    // FNV-1a is used only to make a stable filesystem-safe directory name, not for security.
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

void set_error(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
}

void merge_learned_rules(GameplayObservation& observation, const ServerMechanicsProfile& learned) {
    if (learned.attack_model != AttackModel::Unknown) {
        observation.server_mechanics.attack_model = learned.attack_model;
        observation.server_mechanics.confidence = learned.confidence;
        observation.server_mechanics.valid_trials = learned.valid_trials;
        observation.server_mechanics.full_damage_interval_ticks = learned.full_damage_interval_ticks;
        observation.server_mechanics.minimum_observed_interval_ticks = learned.minimum_observed_interval_ticks;
        observation.server_mechanics.evidence_note = learned.evidence_note;
    }
    if (learned.family_hint != ServerFamilyHint::Unknown) observation.server_mechanics.family_hint = learned.family_hint;
    // Only verified positive capabilities are merged from learned observations. The current
    // profile uses plain bools rather than tri-state capability values, so false means unknown.
    const auto verified = [&learned](ServerFeature feature) {
        return (learned.verified_feature_mask & (1U << static_cast<unsigned int>(feature))) != 0U;
    };
    if (verified(ServerFeature::AxeShieldPunish)) observation.server_mechanics.axe_shield_punish_supported = learned.axe_shield_punish_supported;
    if (verified(ServerFeature::FireballUse)) observation.server_mechanics.fireball_use_supported = learned.fireball_use_supported;
    if (verified(ServerFeature::WindChargeClutch)) observation.server_mechanics.wind_charge_clutch_supported = learned.wind_charge_clutch_supported;
    if (verified(ServerFeature::BedrockScaffolding)) observation.server_mechanics.bedrock_scaffolding_supported = learned.bedrock_scaffolding_supported;
    if (verified(ServerFeature::OffhandUse)) observation.server_mechanics.offhand_supported = learned.offhand_supported;
    if (verified(ServerFeature::CustomItemActions)) observation.server_mechanics.custom_item_actions_supported = learned.custom_item_actions_supported;
}
}

GameplayControlLoop::GameplayControlLoop(GameplayObservationSource& source,
                                         GameplayActionSink& sink,
                                         GameplayBrain brain)
    : source_(source), sink_(sink), brain_(std::move(brain)) {}

bool GameplayControlLoop::configure_learning_directory(const std::string& directory, std::string* error) {
    if (!learning_directory_.empty() || !loaded_profiles_.empty()) {
        set_error(error, "configure learning storage exactly once, before the first gameplay tick"); return false;
    }
    if (directory.empty()) { set_error(error, "learning directory is empty"); return false; }
    std::error_code ec;
    const std::filesystem::path root(directory);
    std::filesystem::create_directories(root, ec);
    if (ec) { set_error(error, "cannot create learning directory: " + ec.message()); return false; }
    std::filesystem::permissions(root, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, ec);
    if (ec) { set_error(error, "cannot restrict learning directory permissions: " + ec.message()); return false; }
    learning_directory_ = root.string();
    return true;
}

std::string GameplayControlLoop::profile_directory(const std::string& profile_key) const {
    if (learning_directory_.empty()) return {};
    return (std::filesystem::path(learning_directory_) / ("profile-" + stable_profile_hash(profile_key))).string();
}

bool GameplayControlLoop::ensure_profile_loaded(const std::string& profile_key, std::string* warning) {
    if (loaded_profiles_.find(profile_key) != loaded_profiles_.end()) return true;
    // Create the in-memory profile first. Profiles are never shared across server/mode keys.
    (void)experience_by_profile_[profile_key];
    (void)items_by_profile_[profile_key];
    (void)rules_by_profile_[profile_key];
    loaded_profiles_.insert(profile_key);
    if (learning_directory_.empty()) return true;

    const std::filesystem::path directory(profile_directory(profile_key));
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) { if (warning) *warning = "cannot create profile memory directory: " + ec.message(); return false; }
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                std::filesystem::perm_options::replace, ec);
    if (ec) { if (warning) *warning = "cannot restrict profile memory directory: " + ec.message(); return false; }

    const auto load_if_present = [&](const std::string& filename, auto&& loader) {
        const auto path = directory / filename;
        std::error_code exists_error;
        if (!std::filesystem::exists(path, exists_error)) {
            if (exists_error && warning) *warning = "cannot inspect profile memory file: " + exists_error.message();
            return !exists_error;
        }
        std::string error;
        if (!loader(path.string(), &error)) {
            if (warning) *warning = filename + " ignored: " + error;
            return false;
        }
        return true;
    };
    bool ok = true;
    ok = load_if_present("skills.tsv", [&](const std::string& path, std::string* error) {
        return experience_by_profile_[profile_key].load_tsv(path, error);
    }) && ok;
    ok = load_if_present("items.tsv", [&](const std::string& path, std::string* error) {
        return items_by_profile_[profile_key].load_tsv(path, error);
    }) && ok;
    ok = load_if_present("server-rules.tsv", [&](const std::string& path, std::string* error) {
        return rules_by_profile_[profile_key].load_tsv(path, error);
    }) && ok;
    return ok;
}

bool GameplayControlLoop::persist_profile(const std::string& profile_key, std::string* warning) const {
    if (learning_directory_.empty()) return true;
    const std::filesystem::path directory(profile_directory(profile_key));
    const auto skills = experience_by_profile_.find(profile_key);
    const auto items = items_by_profile_.find(profile_key);
    const auto rules = rules_by_profile_.find(profile_key);
    std::string error;
    bool ok = true;
    if (skills != experience_by_profile_.end() && !skills->second.save_tsv((directory / "skills.tsv").string(), &error)) ok = false;
    if (ok && items != items_by_profile_.end() && !items->second.save_tsv((directory / "items.tsv").string(), &error)) ok = false;
    if (ok && rules != rules_by_profile_.end() && !rules->second.save_tsv((directory / "server-rules.tsv").string(), &error)) ok = false;
    if (!ok && warning) *warning = error;
    return ok;
}

GameplayTickResult GameplayControlLoop::tick() {
    GameplayTickResult result;
    GameplayObservation observation;
    if (!source_.read(observation, &result.error)) {
        if (result.error.empty()) result.error = "observation source has no fresh, trustworthy game state";
        return result;
    }
    result.observation_ready = true;
    active_profile_key_ = observation.server_profile_key.empty() ? "default" : observation.server_profile_key;
    std::string persistence_warning;
    if (!ensure_profile_loaded(active_profile_key_, &persistence_warning) && !persistence_warning.empty()) {
        result.note = persistence_warning;
    }

    ItemKnowledgeBase& item_knowledge = items_by_profile_[active_profile_key_];
    bool learned_state_changed = false;
    if (observation.verified_attack_trial.has_value()) {
        rules_by_profile_[active_profile_key_].observe_attack_trial(*observation.verified_attack_trial);
        learned_state_changed = true;
    }
    if (observation.verified_item_outcome.has_value()) {
        std::string learning_error;
        if (item_knowledge.record_outcome(observation.verified_item_outcome->item,
                                          observation.verified_item_outcome->success, &learning_error)) {
            learned_state_changed = true;
        }
        // An outcome for an unregistered/ambiguous variant is ignored safely. It cannot
        // auto-authorize an unverified custom action.
    }
    for (InventoryStackObservation& stack : observation.inventory) {
        if (stack.identifier.empty()) continue;
        ItemKnowledge resolved = item_knowledge.resolve(ItemDescriptor{
            stack.identifier, stack.display_name, stack.item_tags, stack.variant_key});
        if (resolved.verified) {
            stack.capabilities = resolved.capabilities;
            stack.activation = resolved.activation;
            stack.activations = resolved.activations;
            if (resolved.projectile_physics.has_value()) stack.custom_projectile_physics = resolved.projectile_physics;
            stack.capability_profile_verified = true;
            stack.profile_confidence = resolved.confidence;
        } else if (!stack.capability_profile_verified) {
            // Candidate tags and display-name hints may guide inspection but never authorize
            // execution. Clear stale guessed activation from an upstream inventory mapper.
            stack.capabilities = resolved.capabilities;
            stack.activation = ItemActivationMode::Unknown;
            stack.activations = 0U;
            stack.capability_profile_verified = false;
            stack.profile_confidence = resolved.confidence;
        }
    }

    merge_learned_rules(observation, rules_by_profile_[active_profile_key_].profile());
    if (learned_state_changed && !persist_profile(active_profile_key_, &persistence_warning) && !persistence_warning.empty()) {
        result.note = "learning persistence warning: " + persistence_warning;
    }
    SkillExperienceBook& active_experience = experience_by_profile_[active_profile_key_];
    result.decision = brain_.decide(observation, &active_experience);
    if (!result.decision.valid) {
        result.note = result.decision.reason;
        return result;
    }
    if (result.decision.action == BrainAction::Observe || result.decision.action == BrainAction::Wait) {
        result.note = result.decision.reason;
        return result;
    }

    result.action_attempted = true;
    std::string sink_error;
    const ActionSubmission submission = sink_.submit(result.decision, &sink_error);
    result.action_submitted = submission.submitted;
    result.note = submission.note;
    if (!submission.submitted) {
        result.error = sink_error.empty() ? "action sink rejected the decision" : sink_error;
        return result;
    }
    if (submission.outcome_observed) {
        const double reward = std::isfinite(submission.reward) ? submission.reward : 0.5;
        active_experience.record(result.decision.skill, submission.outcome_success, reward);
        if (!persist_profile(active_profile_key_, &persistence_warning)) {
            result.note += (result.note.empty() ? "" : "; ") + std::string("learning persistence warning: ") + persistence_warning;
        }
    }
    return result;
}

void GameplayControlLoop::record_outcome(SkillId skill, bool success, double reward) {
    experience_by_profile_[active_profile_key_].record(skill, success, reward);
    std::string ignored;
    (void)persist_profile(active_profile_key_, &ignored);
}

bool GameplayControlLoop::register_item_profile(const std::string& server_profile_key,
                                                const ItemKnowledge& profile, std::string* error) {
    if (server_profile_key.empty() || server_profile_key.size() > 512U) {
        set_error(error, "a stable server+mode profile key is required for custom item profiles"); return false;
    }
    std::string warning;
    if (!ensure_profile_loaded(server_profile_key, &warning) && !warning.empty()) {
        set_error(error, warning); return false;
    }
    if (!items_by_profile_[server_profile_key].register_profile(profile, error)) return false;
    return persist_profile(server_profile_key, error);
}

bool GameplayControlLoop::record_item_outcome(const std::string& identifier, bool success, std::string* error) {
    if (!items_by_profile_[active_profile_key_].record_outcome(identifier, success, error)) return false;
    return persist_profile(active_profile_key_, error);
}

bool GameplayControlLoop::record_item_outcome(const ItemDescriptor& descriptor, bool success, std::string* error) {
    if (!items_by_profile_[active_profile_key_].record_outcome(descriptor, success, error)) return false;
    return persist_profile(active_profile_key_, error);
}

void GameplayControlLoop::observe_attack_trial(const AttackTrial& trial) {
    rules_by_profile_[active_profile_key_].observe_attack_trial(trial);
    std::string ignored;
    (void)persist_profile(active_profile_key_, &ignored);
}

void GameplayControlLoop::observe_server_family_hint(ServerFamilyHint hint, bool explicit_evidence) {
    rules_by_profile_[active_profile_key_].observe_server_family_hint(hint, explicit_evidence);
    std::string ignored;
    (void)persist_profile(active_profile_key_, &ignored);
}

void GameplayControlLoop::observe_server_feature(ServerFeature feature, bool supported, bool verified_server_result) {
    rules_by_profile_[active_profile_key_].observe_feature(feature, supported, verified_server_result);
    std::string ignored;
    (void)persist_profile(active_profile_key_, &ignored);
}

const ItemKnowledgeBase* GameplayControlLoop::item_knowledge_for_active_profile() const {
    const auto found = items_by_profile_.find(active_profile_key_);
    return found == items_by_profile_.end() ? nullptr : &found->second;
}

ServerMechanicsProfile GameplayControlLoop::learned_server_mechanics_for_active_profile() const {
    const auto found = rules_by_profile_.find(active_profile_key_);
    if (found != rules_by_profile_.end()) return found->second.profile();
    return {};
}

bool GameplayControlLoop::save_learning_state(std::string* error) const {
    if (learning_directory_.empty()) { set_error(error, "learning directory is not configured"); return false; }
    return persist_profile(active_profile_key_, error);
}

bool GameplayControlLoop::save_skill_memory(const std::string& path, std::string* error) const {
    const auto found = experience_by_profile_.find(active_profile_key_);
    if (found != experience_by_profile_.end()) return found->second.save_tsv(path, error);
    const SkillExperienceBook empty;
    return empty.save_tsv(path, error);
}

bool GameplayControlLoop::load_skill_memory(const std::string& path, std::string* error) {
    return experience_by_profile_[active_profile_key_].load_tsv(path, error);
}

const SkillExperienceBook& GameplayControlLoop::experience() const {
    const auto found = experience_by_profile_.find(active_profile_key_);
    if (found != experience_by_profile_.end()) return found->second;
    static const SkillExperienceBook empty;
    return empty;
}

const std::string& GameplayControlLoop::active_profile_key() const { return active_profile_key_; }

} // namespace bedrockai
