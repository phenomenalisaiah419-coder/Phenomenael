#include "companion_brain_adapter.hpp"

#include "core/control_hub.hpp"

#include <cmath>
#include <string>

namespace bedrockai::companion {

std::string run_native_safety_self_check() {
    ControlHub hub;
    HubInput input;
    input.tick = 1U;
    input.server_key = "companion.stage1.unverified";
    input.server_class = ServerClass::Unknown;
    input.requested_level = AssistLevel::Takeover;
    input.edition = GameEdition::Bedrock;

    const HubOutput output = hub.tick(input);
    const bool command_values_finite =
        std::isfinite(output.cmd.move_forward) && std::isfinite(output.cmd.move_strafe) &&
        std::isfinite(output.cmd.look_yaw_deg) && std::isfinite(output.cmd.look_pitch_deg);
    const bool no_game_action =
        !output.cmd.attack && !output.cmd.place && !output.cmd.throw_pearl && !output.cmd.use_water &&
        !output.cmd.look_override && !output.cmd.jump && !output.cmd.sprint && !output.cmd.sneak &&
        output.cmd.move_forward == 0.0 && output.cmd.move_strafe == 0.0;
    const bool observe_only = output.effective_level == AssistLevel::ObserveOnly;
    const bool safe = observe_only && no_game_action && command_values_finite;

    return std::string(safe ? "READY" : "FAILED") +
        ";engine=ControlHub-v0.35;policy=" + assist_level_name(output.effective_level) +
        ";game_state=unavailable;actions=" + (no_game_action ? "none" : "unexpected") +
        ";finite=" + (command_values_finite ? "yes" : "no");
}

} // namespace bedrockai::companion
