# Gameplay Intelligence Core — v0.28

Target project: Minecraft Bedrock 1.26.50 / protocol 2193. This is a source module for the Termux/controller project, not an Android app.

## What has now been implemented

### 1. Situation-based tactical decision engine

`core/include/core/gameplay_brain.hpp` and `core/src/gameplay_brain.cpp` add `GameplayBrain`. It chooses one short action from a typed observation of the current world, player condition, available hotbar items, known threats, current objective, and measured server capabilities.

Decision priority is deliberately hierarchical:

1. Known void-fall emergencies, with validated recovery options only.
2. Fire and cobweb recovery.
3. Food consumption only when hunger is known and a hostile threat is not observed.
4. Critical/moderate health decisions based on a caller-provided safe-use window and a known threat.
5. Explicitly observed mace and wind-charge opportunity windows.
6. Shield response, item choice, and calibrated ranged attacks.
7. Mining, bridging, BedWars objective and SMP travel goals.
8. General melee target/style selection using existing movement, aim and combat controllers.

A `GameplayDecision` includes action, skill ID, target, item slot, confidence, reason, movement/aim output and (where available) a projectile solution. It is a one-step decision; the caller should reobserve after execution instead of running a long open-loop combo.

### 2. Prebuilt skill catalogue

`gameplay_skill_catalog()` currently supplies 35 typed definitions covering perception/planning, melee styles, sword strafing, axe use, shield defence, bow/crossbow, mace, wind charge, pearl escape/chase, water/block clutch, eating/healing, fire and web recovery, bridging styles, mining, resource gathering, BedWars roles, SMP travel, projectile dodging, sprint-jump, circling and retreat/reset.

**A catalogue definition is not proof that every skill has a complete executor.** In this version, the tactical decision paths described above have code; other entries define the intended skill contract for subsequent implementation.

### 3. Calibrated projectile solver

`ProjectileSolver` integrates a configured projectile profile and searches for yaw, pitch and flight time against a linearly moving target, including estimated packet latency. It rejects invalid physics parameters and refuses to claim a shot if the predicted miss exceeds the configured tolerance.

The physics parameters are not hard-coded Java or guessed Bedrock values. Each item profile must be calibrated for the intended Bedrock version/server before it is marked `calibrated = true`. Profiles are intentionally uncalibrated by default, so bow, pearl, potion, fireball and wind-charge trajectories do not pretend to be reliable out of the box.

### 4. Outcome memory

`SkillExperienceBook` records success/failure plus a bounded reward for individual skill IDs, computes a smoothed effectiveness estimate, and can save/load a versioned TSV file. The file stores skill statistics only; it has no account tokens or authentication keys and is written with owner-only read/write permissions.

This is **not yet end-to-end autonomous learning**. The gameplay runtime must still record verified outcomes after executing a skill. The engine uses supplied records to bias later decisions, and tests prove that sufficiently different outcomes can change a recovery choice.

## Bedrock-specific safeguards

- Melee attack proposals stay disabled until `GameplayCalibration.melee_reach` contains a finite measured value within the accepted sanity range. No extra reach is added to bypass server distance validation.
- Void escape is chosen only when the observation reports the fall/void hazard and the item, safe site and/or calibrated capability are present.
- Pearl aiming requires a known safe landing and a calibrated pearl profile.
- Mace smash requires a mode-appropriate mace, plus a caller-confirmed smash opportunity. It does not infer a successful mace window from height alone.
- Wind-charge knockback requires a confirmed opportunity and calibrated projectile trajectory. Wind-charge clutch requires the recovery profile to be marked calibrated.
- Water clutch is disabled in the Nether and requires server/placement permission plus a known clutch site.
- Vanilla Bedrock axe PvP must not inherit Java's shield-disabling assumption. The axe counter-strategy is disabled unless a custom-server profile explicitly confirms the behaviour. See the Bedrock comparison in the references.
- Item slots in this release are zero-based hotbar slots (0–8). Automatic movement of items from the main inventory into the hotbar is not yet implemented.
- Unknown health, inventory, line-of-sight, enemy equipment, target safety and projectile physics must remain unknown; the brain must not invent them.

## Current boundary: not yet live-wired

This is the first implemented gameplay-intelligence layer, **not the complete autonomous player yet**. `GameplayBrain` consumes `GameplayObservation`; the live controller does not yet produce every field from decoded Bedrock play packets or feed the resulting actions back into the encrypted network loop.

The existing `Protocol2193NetworkEngine` can deliver validated packet frames, but the full version-2193 state bridge still needs work for player/entity metadata, inventory contents/equipment, effects, nearby blocks, target visibility, and reliable server corrections. The current `PlayerAuthInputCodec` deliberately rejects item-interaction, item-stack-request and block-action payloads. Thus this version must not be described as already able to eat, drink, throw, use blocks, equip, mine or execute bow attacks on a live server.

The next implementation needs to:

1. Decode the 1.26.50 clientbound packets into versioned player/entity/inventory/equipment/effect/block observations.
2. Add protocol-2193 item-use / item-stack-request / block-action codecs and the necessary equipment/slot operations.
3. Compile each `GameplayDecision` into legal Bedrock packets; send one bounded action slice, then wait for authoritative updates.
4. Record success/failure from server-confirmed outcomes and persist `SkillExperienceBook` in private Termux storage.
5. Add replay fixtures and then calibrate mechanics in a permitted practice world. Do not mark projectile or reach profiles calibrated merely because a trajectory mathematically solved.

## References

- [Official versioned Bedrock protocol docs — `PlayerAuthInputPacket` for 1.26.50 / protocol 2193](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/player-auth-input-packet/). The current schema documents server-authoritative movement inputs and the packed optional item-use, stack-request, and block-action payloads. Its 1.26.50 changelog records field changes for `ItemUseInventoryTransaction`; codecs must be version-specific.
- [Official Bedrock Preview 1.20.80.22 — Mace](https://www.minecraft.net/en-us/article/minecraft-preview-1-20-80-22). Documents fall-distance-based smash damage, fall-damage mitigation on a successful smash, and use with wind charges.
- [Official Bedrock Preview 1.20.70.24 — Wind Charge](https://www.minecraft.net/en-us/article/minecraft-preview-1-20-70-24). Documents player-fired wind-charge effects, direct-hit damage, cooldown and fall-damage interaction in that preview; later version notes must be checked before treating a number as current.
- [Official Bedrock 1.21.20 notes — Wind Charge changes](https://feedback.minecraft.net/hc/en-us/articles/29110275666957-Minecraft-1-21-20-Bedrock). Records later adjustments including player-fired speed, downward-use launch and latency-related fall-damage fixes.
- [Minecraft Wiki — Tutorial: Player versus Player](https://minecraft.wiki/w/Tutorial:Player_versus_Player). Useful for naming PvP kit types, but it is not a substitute for measuring a server's custom rules.
- [Minecraft Wiki — Tutorial: Combat](https://minecraft.wiki/w/Tutorial:Combat). Its Bedrock comparison explicitly warns that axes do not inherit the Java shield-disable advantage in vanilla Bedrock.

These sources establish mechanics to investigate, not proof that the controller has executed every named skill successfully. Server plugins, custom kits and version changes can alter the result.
