# v0.34 — local-player movement feedback bridge

## Goal

Connect the protocol-2193 observation decoder to `MovementActionSender` without allowing unrelated actor packets or a generic world tick to release the one-pending-action gate.

## Protocol rule

The version-pinned [PlayerInputTick schema](https://mojang.github.io/bedrock-protocol-docs/1.26.50/types/player-input-tick/) says that clientbound packet ticks should correspond to the most recently processed `PlayerAuthInputPacket`. The [CorrectPlayerMovePrediction packet](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/correct-player-move-prediction-packet/) identifies the input tick being corrected. Tick zero is not usable feedback.

## Code contract

`ObservationDecodeReport::local_player_input_tick` is reset at the start of every `accept_frame` call. It is set only after successful structural decode and only when the packet's identity can be tied to this local player. Supported evidence sources in this slice are:

- `MovePlayer` for the observed local runtime ID, including a non-zero tick.
- `CorrectPlayerMovePrediction` for the player (`prediction_type == 0`), not the vehicle variant.
- `UpdateAttributes` targeted at the local runtime ID.
- `MobEffect` targeted at the local runtime ID.
- `UpdatePlayerGameType` for the local unique ID.

A tick on a packet for another actor is not tagged as local-player evidence, even if the general world snapshot has a high `server_tick`. Tick values that are zero are omitted from the report. Malformed frames return failure and do not acknowledge an action.

## Host integration

The host should route each incoming decoded clientbound frame through the bridge for this movement sender, rather than calling `decoder.accept_frame()` a second time for the same frame:

```cpp
wire::MovementFeedbackBridge feedback_bridge;
ObservationDecodeReport report;
bool movement_acknowledged = false;
std::string error;

if (!feedback_bridge.process_frame(decoder, frame, movement_sender,
                                   &report, &movement_acknowledged, &error)) {
    // Preserve the decode error and fail closed for this frame.
}
if (movement_acknowledged) {
    // The server input tick reached or passed the pending action tick.
}
```

`process_frame` returns whether frame decoding succeeded; `movement_acknowledged` separately reports whether this frame actually released a pending action. A locally valid tick with no pending action is harmless. A tick lower than the pending action tick is not an acknowledgement.

## Validation evidence

Focused `v34_tests` cover local correction ticks, lower/stale tick behavior, unrelated-player movement ticks, unrelated-player game-mode ticks, local MovePlayer tick parsing, tick-zero behavior, and malformed correction rejection. GCC 14 Release strict-warning build of `core` plus this test target passed. Full-suite validation and a real 1.26.50 server/physical Termux session are still required.

## Explicit boundary

This bridge is a source-level integration seam; it is not yet wired into every Android or Termux session host in production. It does not implement item use, attack, stack requests, block actions, or automatic movement replanning. No live server or physical-device result is claimed.
