#pragma once

#include "core/gameplay_brain.hpp"
#include "core/server_rules.hpp"
#include "core/item_knowledge.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace bedrockai {

class GameplayObservationSource {
public:
    virtual ~GameplayObservationSource() = default;
    // Implemented by the live protocol/session decoder. False means no trustworthy tick is ready.
    virtual bool read(GameplayObservation& observation, std::string* error) = 0;
};

struct ActionSubmission {
    bool submitted = false;
    // A packet write succeeding is not the same as the game action succeeding.
    bool outcome_observed = false;
    bool outcome_success = false;
    double reward = 0.5;
    std::string note;
};

class GameplayActionSink {
public:
    virtual ~GameplayActionSink() = default;
    // Must translate only supported decisions to valid Bedrock actions. It should update
    // PlayerState::last_attack_tick only after an attack input was actually emitted.
    virtual ActionSubmission submit(const GameplayDecision& decision, std::string* error) = 0;
};

struct GameplayTickResult {
    bool observation_ready = false;
    bool action_attempted = false;
    bool action_submitted = false;
    GameplayDecision decision{};
    std::string error;
    std::string note;
};

// Thin orchestration loop between a state decoder, the adaptive brain, and a protocol
// action encoder. It contains no fake decoder/encoder: hosts supply the real session adapters.
class GameplayControlLoop {
public:
    GameplayControlLoop(GameplayObservationSource& source,
                        GameplayActionSink& sink,
                        GameplayBrain brain = GameplayBrain{});
    GameplayTickResult tick();
    void record_outcome(SkillId skill, bool success, double reward = 1.0);
    // Optional persistence directory. When configured, each server+mode profile is hashed
    // to a separate private subdirectory and skill/item/rules memory is loaded/saved there.
    bool configure_learning_directory(const std::string& directory, std::string* error = nullptr);
    bool register_item_profile(const std::string& server_profile_key, const ItemKnowledge& profile,
                               std::string* error = nullptr);
    bool record_item_outcome(const std::string& identifier, bool success, std::string* error = nullptr);
    bool record_item_outcome(const ItemDescriptor& descriptor, bool success, std::string* error = nullptr);
    void observe_attack_trial(const AttackTrial& trial);
    void observe_server_family_hint(ServerFamilyHint hint, bool explicit_evidence);
    void observe_server_feature(ServerFeature feature, bool supported, bool verified_server_result);
    const ItemKnowledgeBase* item_knowledge_for_active_profile() const;
    ServerMechanicsProfile learned_server_mechanics_for_active_profile() const;
    bool save_learning_state(std::string* error = nullptr) const;
    bool save_skill_memory(const std::string& path, std::string* error = nullptr) const;
    bool load_skill_memory(const std::string& path, std::string* error = nullptr);
    const SkillExperienceBook& experience() const;
    const std::string& active_profile_key() const;

private:
    GameplayObservationSource& source_;
    GameplayActionSink& sink_;
    GameplayBrain brain_;
    std::unordered_map<std::string, SkillExperienceBook> experience_by_profile_;
    std::unordered_map<std::string, ItemKnowledgeBase> items_by_profile_;
    std::unordered_map<std::string, ServerRulesLearner> rules_by_profile_;
    std::unordered_set<std::string> loaded_profiles_;
    std::string learning_directory_;
    std::string active_profile_key_ = "default";
    bool ensure_profile_loaded(const std::string& profile_key, std::string* warning = nullptr);
    std::string profile_directory(const std::string& profile_key) const;
    bool persist_profile(const std::string& profile_key, std::string* warning = nullptr) const;
};

} // namespace bedrockai
