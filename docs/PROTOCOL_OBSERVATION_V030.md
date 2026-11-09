# Bedrock 1.26.50 observation decoder — v0.30

Target: Minecraft Bedrock 1.26.50, protocol 2193. This is a Termux/controller source module, not an Android app.

## Purpose

v0.30 implements the first version-pinned inbound gameplay-state decoder. It turns a strict subset of authenticated, decoded Bedrock packet frames into the existing `GameplayObservation`/`WorldSnapshot` model. It is intentionally conservative: a packet with unknown layout is reported as unsupported, a truncated/invalid packet is malformed, and information that the server has not actually provided remains unknown.

The decoder sits after the secure network layer. It does not decode RakNet datagrams, Minecraft encryption, batch compression, or the Bedrock packet-header layer itself; it consumes `TransportFrame` objects that have already passed those layers. The native controller's `read_gameplay_observation()` exposes the in-memory snapshot to a future decision-loop host, but the complete decision → outgoing action → server-confirmed feedback loop is not yet wired end to end.

## Implemented clientbound packet handlers

| Packet | ID | State updated | Limitations |
|---|---:|---|---|
| StartGame | 11 | Local runtime/unique IDs, game mode prefix, position, rotation, joined-world flag | Partial prefix only; trailing fields deliberately not decoded |
| AddPlayer | 12 | Runtime/unique IDs, UUID/name, position, velocity, rotation/head rotation, player marker | Partial prefix; carried item, attributes and metadata not decoded; other players are not hostile by default |
| AddActor | 13 | Runtime/unique IDs, actor identifier/type string, position, velocity, rotation | Partial prefix; attributes and metadata not decoded; unknown actor hostility remains false/unknown |
| RemoveActor | 14 | Removes the known non-player entity | Unknown IDs do not create state |
| MoveActorAbsolute | 18 | Entity position, packed rotation, grounded/teleport flags | Header flags are validated; not an entity metadata decoder |
| MovePlayer | 19 | Local or known entity position, rotation/head-only mode, grounded state, riding/teleport fields, actual PlayerInputTick | Teleport extras are consumed only for Teleport mode; OnlyHeadRot does not overwrite position |
| MobEffect | 28 | Add/update/remove effect records for known player/entity | Does not infer effects that are not sent |
| SetActorMotion | 40 | Entity velocity | Requires known runtime ID |
| SetHealth | 42 | Local health after StartGame | Health only; max health/attributes still need UpdateAttributes parsing |
| MoveActorDelta | 111 | Relative entity position/rotation, ground/force flags | Optional fields follow the validated low-six-bit flags; packet `Ticks` is not treated as server input tick |
| UpdatePlayerGameType | 151 | Local game mode when target unique ID matches | Ignores game-mode updates for other players |
| MotionPredictionHints | 157 | Entity motion and grounded hint | Hints do not override actual position |
| CorrectPlayerMovePrediction | 161 | Local player correction position/velocity/ground and correction tick | Vehicle-specific rotation fields are consumed for vehicle correction; no vehicle model is built yet |

The decoder keeps a bounded per-packet counter, bounds entity/effect collections, validates finite numeric values, checks frame headers and rejects malformed optional-field combinations. Player classification is distinct from mob hostility; the caller must opt into treating other players as targets. A “cracked” login/access method is not treated as a combat-rule signal.

## Important tick distinction

`WorldSnapshot.tick` is the local observation/frame sequence counter. It is **not** the game's server tick. `WorldSnapshot.server_tick` plus `server_tick_known` records a version-defined input/correction tick only when one is actually present. Cooldown gating must use the protocol tick and the tick of the last attack actually emitted; it must not count received packets or UI poll calls as ticks. Missing server-tick evidence causes a cooldown profile to fail closed rather than spam attacks.

## Intentionally unsupported in v0.30

- `InventoryContent` (49) and `InventorySlot` (50), including `NetworkItemStackDescriptor` and version-specific item components/variant data.
- `MobEquipment` (31) and `MobArmorEquipment` (32).
- `UpdateAttributes` (29), including hashed attribute names/modifiers and max-health/attack attributes.
- `SetActorData`/entity metadata, potion details, item enchantment/durability descriptions where not decoded, and hidden enemy inventory/health.
- Level chunks/subchunks, block updates, collision/block-state map, block-face/raycast validation.
- Item-stack-request, item-use inventory transaction, outgoing block actions, equipment changes, use/consume/throw packets and other outgoing executors.
- Reliable end-to-end server-confirmed results for each gameplay action.

The packet ID numbers alone are not enough to decode an item stack safely. In particular, inventory stacks can contain variant/component descriptors, not merely a numeric item ID and count. v0.30 does not guess those nested layouts. Therefore its decoder will not yet automatically populate the player's live inventory and armor from the wire. `replace_inventory()` is a test/host injection point for already verified observations, not a claim that live inventory decoding is finished.

## Protocol references

Use the official versioned 1.26.50 schema for every handler:

- [Protocol changelog](https://mojang.github.io/bedrock-protocol-docs/changelog/)
- [StartGame](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/start-game-packet/)
- [AddActor](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/add-actor-packet/)
- [MoveActorAbsolute](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/move-actor-absolute-packet/)
- [MovePlayer](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/move-player-packet/)
- [PlayerAuthInput](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/player-auth-input-packet/)
- [SetHealth](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/set-health-packet/)
- [MoveActorDelta](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/move-actor-delta-packet/)
- [UpdatePlayerGameType](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/update-player-game-type-packet/)
- [MotionPredictionHints](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/motion-prediction-hints-packet/)
- [CorrectPlayerMovePrediction](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/correct-player-move-prediction-packet/)
- [InventoryContent](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/inventory-content-packet/)
- [InventorySlot](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/inventory-slot-packet/)
- [MobEquipment](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/mob-equipment-packet/)
- [MobArmorEquipment](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/mob-armor-equipment-packet/)
- [UpdateAttributes](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/update-attributes-packet/)

## Verification

`v30_tests` supplies hand-built protocol frames and covers packet application, malformed/truncated payloads, unsupported inventory packets, state transitions, rotation-only movement, server tick tracking, health/effects, actor motion and game-mode/correction handling. This demonstrates the implemented prefix handlers and validation, not a full packet capture or a live Hive session. A captured, secret-free, protocol-2193 packet fixture set and a real controlled server remain required before claiming live-state fidelity.
