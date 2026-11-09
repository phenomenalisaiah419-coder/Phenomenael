# Host integration: how to drive `ControlHub`

One call per game tick; one command back. The hub never opens a socket or touches game memory.

```cpp
HubConfig cfg;
cfg.attack_reach = measured_reach;      // from the mechanics probe; unset = the AI never attacks
ControlHub hub(cfg);
hub.policy().grant({server_key, AssistLevel::Takeover, /*user_confirmed_rules_allow=*/true});  // only if the rules allow it

HubInput in;                            // fill every tick
in.tick = server_or_local_tick;  in.feet = feet_position;  in.vertical_velocity = vy;  in.on_ground = on_ground;
in.yaw_deg = yaw;  in.pitch_deg = pitch;  in.health = hp;  in.max_health = max_hp;
in.grid = &world_grid;                  // WorldGrid built from chunk packets (Unknown stays Unknown)
in.human = touch_frame;                 // HumanIntentFrame from the touch layer
in.kit = {blocks, has_pearl, pearl_calibrated, pearl_physics, has_water_bucket};
in.enemies = nearby_entities;           // id, position, health, hostile
in.server_key = "host:port|mode";  in.server_class = ServerClass::OwnWorld;
in.requested_level = AssistLevel::Assist;     // what the user toggled in the app

HubOutput out = hub.tick(in);           // out.cmd is what to send; out.explanation / out.denials are for the UI
```

## Contract

1. **Positions are feet positions.** Bedrock sends eye-level positions: subtract 1.62 at the decoder boundary (finding F5 in the audit; confirm with one live capture).
2. `cmd.move_forward/strafe` are relative to `cmd.look_yaw_deg` when `cmd.look_override` is true (PlayerAuthInput interprets the move vector against the rotation in the same packet); otherwise relative to the current yaw. Apply the look first.
3. `cmd.attack/place/throw_pearl/use_water` are **AI-originated** actions. The human's own swings and placements pass through the game as usual.
4. `HumanIntentFrame::look_dx/look_dy` are converted with `HubConfig::look_to_degrees`.
5. Only submit an action through a verified action path (`MovementActionSender` today). Item-use and block-action codecs are still to be written; until then run the hub in **recommendation mode** (log `out.cmd`, send nothing) on a controlled server.
6. Report nothing manually: outcomes are learned from the health and enemy-health values the host passes each tick. Save with `hub.learner().save(path)`.
7. The user-facing controls to build: assist level (Off / Survival only / Assist / Takeover), the explicit override button (`HumanIntentFrame::explicit_override`), a panic key (`hub.authority().panic_release()`), a status line (`out.authority.state`, `out.technique`, `out.explanation`, `out.denials`).

## Fair play

`AutomationPolicy` caps the level by server class. Unknown and public servers are observe-only until the user records that the server's rules allow automation.
