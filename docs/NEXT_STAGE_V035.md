# Next stage after v0.35 (ordered)

1. **Chunk to `WorldGrid`.** Decode `LevelChunk`/`SubChunk` (fail closed on unknown), build the block-palette classifier, mark loaded columns. Without this the guardian only sees what tests give it.
2. **Close the movement loop (spec P1/P2).** Feed server ticks/corrections into `MovementActionSender`, drive it from `HubCommand`, run on a controlled BDS (`transport=raknet`) from Termux. First live evidence: halt at an edge.
3. **Item-use and block-action codecs** (`ItemUseInventoryTransaction`, `PlayerAction`, `ItemStackRequest`) as strict slices with receipts, then live-verify block place, bucket and pearl.
4. **Mechanics probe on the real server**: reach (attack/place), cadence, jump, fall, knockback, pearl and bucket reach. Feed `HubConfig::attack_reach`, `GuardianConfig::placement_reach/water_reach`, `ProjectilePhysics`.
5. **Resolve the same-character path** (embedded client / verified hooks / custom client). The headless client is a lab path; it is a second player.
6. **Promote Scripted techniques to Executable** one at a time using probe data (w-tap, jump reset, crit jump, parkour gaps, wind-charge, god/ninja bridging). Each needs a simulator test, then a live test.
7. **Persist `ItemLearner`**, add opponent-model persistence, add a `report_item_trial` path from decoded item-use effects.
8. **Java adapter and offline login** (separate wire codecs; the hub is already edition-aware).
9. **Benchmarking**: record fights, define a pre-registered win-rate protocol vs scripted baselines and human testers before using the word "better".

## Successor AI prompt

You are the lead implementer for the Bedrock AI Controller (Bedrock 1.26.50 / protocol 2193), repository version 0.35.
Mission: a client-side AI that shares the owner's single character: it predicts the human's intent, continues actions, overrules the human only to save their life, hands control back after the human's second deliberate input, and learns which skills work against which opponents.
Read first: `docs/AUDIT_V035.md`, `docs/HOST_INTEGRATION_V035.md`, `core/include/core/control_hub.hpp`, `docs/IMPLEMENTATION_STATUS.md`.
Rules that never change: one character; never break beds; no economy actions; no attack without measured reach and the human's attack input; normal placement needs the human's place input; no fabricated offsets or physics; no anti-cheat bypass or hidden automation; unknown is never air; fail closed.
Owner is a non-coder on Android + Termux: give exact commands, ask for screenshots after each gate, state the stage number after each milestone.
Do not call anything "flagship" or "better than top players" without live evidence. Start with step 1 and 2 above, on a controlled server only.
