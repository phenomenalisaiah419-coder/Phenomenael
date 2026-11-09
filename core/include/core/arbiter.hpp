#pragma once

#include "human_intent.hpp"
#include <optional>
#include <string>

namespace bedrockai {

enum class ControlMode { Human, Assist, Takeover, Emergency };
enum class Priority { HumanOverride, ImmediateSurvival, CriticalSurvival, BedlessSurvival, Combat, Positioning, Building, Navigation };

enum class ActionKind { None, Move, Look, Jump, Sneak, Sprint, Attack, PlaceBlock, Mine, SwitchItem, Pearl, WindCharge, Fireball, Potion, KnockbackItem, Flight, Bridge, BuildCover, BuildTrap, HumanOverride, ReleaseControl };

enum class Channel { Movement, Look, Jump, Sneak, Sprint, Attack, Place, Item, Utility };

enum class UtilityKind { None, Pearl, WindCharge, Fireball, Potion, KnockbackItem, Flight };

struct InventoryState {
    bool has_pearl=false, has_wind_charge=false, has_fireball=false, has_potion=false, has_knockback_item=false, has_flight=false;
};

struct CapabilityProfile {
    std::string minecraft_version = "1.26.50";
    std::uint32_t protocol_version = 2193;
    bool ai_can_break_bed = false;
    bool ai_can_mine_ordinary_blocks = true;
    bool ai_can_autonomous_place = false;
    bool ai_can_autonomous_attack = false;
    bool survival_override_can_place = true;
    bool survival_override_can_use_utilities = true;
    bool survival_override_can_move = true;
    bool survival_override_can_look = true;
    bool survival_override_can_switch_item = true;
};

struct HumanInput { HumanIntentFrame touch; int selected_slot = -1; };
struct Action { ActionKind kind=ActionKind::None; bool emergency_allowed=false; UtilityKind utility=UtilityKind::None; };
struct AiProposal {
    ControlMode mode=ControlMode::Assist;
    Priority priority=Priority::Navigation;
    std::optional<Action> movement;
    std::optional<Action> look;
    std::optional<Action> attack;
    std::optional<Action> place;
    std::optional<Action> item;
    std::optional<Action> utility;
    bool emergency=false;
};

struct ControlFrame {
    bool movement=false, look=false, jump=false, sneak=false, sprint=false, attack=false, place=false, item=false, utility=false;
    std::string denial_reason;
};

ControlFrame arbitrate(const CapabilityProfile&, const InventoryState&, const HumanInput&, const AiProposal&);

} // namespace bedrockai
