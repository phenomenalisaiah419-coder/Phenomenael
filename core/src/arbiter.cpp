#include "core/arbiter.hpp"
#include <cmath>
namespace bedrockai {
ControlFrame arbitrate(const CapabilityProfile& c,const InventoryState& inv,const HumanInput& h,const AiProposal& ai){
    ControlFrame out;
    if(h.touch.explicit_override){ out.denial_reason="human override"; return out; }
    const bool emergency=ai.emergency || ai.mode==ControlMode::Emergency;
    out.movement=ai.movement.has_value() || std::abs(h.touch.move_x)>0.001 || std::abs(h.touch.move_y)>0.001;
    out.look=ai.look.has_value() || std::abs(h.touch.look_dx)>0.001 || std::abs(h.touch.look_dy)>0.001;
    out.jump=ai.mode!=ControlMode::Human || h.touch.jump_held;
    out.sneak=ai.mode!=ControlMode::Human || h.touch.sneak_held;
    out.sprint=ai.mode!=ControlMode::Human || h.touch.sprint_held;
    if(ai.attack){ out.attack=h.touch.attack_held || (emergency && c.ai_can_autonomous_attack); }
    if(ai.place){ out.place=h.touch.place_held || (emergency && c.survival_override_can_place && ai.place->emergency_allowed); }
    if(ai.item) out.item=emergency && c.survival_override_can_switch_item;
    if(ai.utility && emergency && c.survival_override_can_use_utilities){
        switch(ai.utility->utility){
            case UtilityKind::Pearl: out.utility=inv.has_pearl; break;
            case UtilityKind::WindCharge: out.utility=inv.has_wind_charge; break;
            case UtilityKind::Fireball: out.utility=inv.has_fireball; break;
            case UtilityKind::Potion: out.utility=inv.has_potion; break;
            case UtilityKind::KnockbackItem: out.utility=inv.has_knockback_item; break;
            case UtilityKind::Flight: out.utility=inv.has_flight; break;
            default: out.utility=false;
        }
    }
    return out;
}
}
