# v0.33 — outbound movement action slice

## Scope

This release adds `wire::MovementActionSender`, a movement/rotation-only send boundary for protocol 2193 (Bedrock 1.26.50). It encodes a `PlayerAuthInput` payload and uses the existing `Protocol2193NetworkEngine` to batch, compress, encrypt when configured, and send the packet through the attached transport backend.

## Guardrails

- Requires `TransportState::InGame`.
- Requires a nonzero, strictly increasing client action tick.
- Allows one pending movement action at a time.
- Rejects non-finite or out-of-range position, rotation, movement, delta, camera, analogue and raw movement values.
- Refuses optional item interaction, item-stack request, block action, and vehicle payloads; this is a movement-only slice.
- Requires the engine batch queue to be empty before accepting a movement action.
- Does not automatically retry after a failed/ambiguous flush. The pending action remains locked until feedback or session-level recovery explicitly resets the sender.
- `observe_server_tick()` releases the pending action only when the observed authoritative tick is at least the submitted tick. The caller must wire a trusted server observation to this method; this release does not claim that wiring is complete.

## Verification

- GCC strict Release: full project build succeeded; CTest 33/33 passed.
- Clang strict Release: v0.31, v0.32 and v0.33 focused test targets passed.
- Sanitizer build: attempted, but compilation exceeded the available command time limit before reaching the new test. No sanitizer pass is claimed for v0.33.
- No physical Android/Termux test and no live Bedrock server test were performed.

## Not implemented here

No attack, item-use, item-stack-request, equipment-switch, inventory-request, block placement/breaking, or autonomous planner feedback integration is claimed. This is one verified outbound movement slice, not a completed autonomous controller.
