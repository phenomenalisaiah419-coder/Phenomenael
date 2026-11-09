# Coherent Native Frame — v0.25

## Purpose

v0.24 verified that a modified 1.26.50 client can attest five native translator targets. v0.25 adds the runtime invariant that those translated events must form one coherent player-control frame before AI actions are generated.

## Frame contract

1. Every translated event carries `ClientHookEventMetadata` containing ABI version, protocol 2193, and the exact tick.
2. `ReadState` establishes the native tick generation and its metadata tick must equal `state.tick`.
3. `ReadInventory` and `ReadTouch` are accepted only after a valid state for the exact same metadata tick.
4. Each source is single-use for a generation.
5. A `Tick` event consumes only a complete state/inventory/touch tuple whose metadata tick matches the pending generation.
6. A consumed or backwards generation is rejected.
7. Consumption clears the pending frame, preventing accidental reuse of stale native pointers or snapshots.

The assembler copies the translated view objects immediately; it never retains raw native pointers after the hook callback returns. The event metadata closes the remaining temporal aliasing hole: inventory/touch samples cannot be associated with an adjacent tick by accident. Entity and block arrays are validated before conversion. Duplicate entity runtime IDs and duplicate/colliding block observations are rejected instead of silently overwriting an `unordered_map` entry.

## Numeric and resource guards

The native state path rejects non-finite player/entity/correction values, negative health, null collection pointers, and collection sizes above the existing controller bounds. Touch and action numeric channels are also checked for finite values. No Minecraft-specific physical limits are invented here.

## Concurrency

The assembler uses a non-blocking atomic gate. Contending hook events fail closed instead of blocking the game thread. Readiness flags are atomic because the native hook dispatcher can receive callbacks concurrently during teardown or abnormal client behavior. The dispatch registration ABI is now version 2 because the event metadata is part of every callback contract.

## Complete-hook behavior

`EmbeddedBedrockClientIntegration::install_complete_hook_plan()` enables coherent native-frame mode after the translated hook registration succeeds and before physical installation. Therefore a production five-site integration cannot silently fall back to re-reading state through the legacy callback path.

## Still required

This layer does not fabricate Mojang's private ABI. The exact authorized 1.26.50 ARM64 client must still translate its real native arguments into the controller-owned typed views and obey the frame event contract. Hardware validation must measure actual callback ordering, latency, re-entry, teardown, and action reconciliation.
