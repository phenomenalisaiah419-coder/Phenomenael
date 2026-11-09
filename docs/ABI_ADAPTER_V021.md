# Exact Typed Client ABI Adapter — v0.21

v0.21 adds a typed adapter boundary for the five real client sites:

- Tick
- ReadState
- ReadInventory
- ReadTouch
- SubmitActions

The core never converts an arbitrary address directly into one of these function-pointer types. The modified client supplies an `ClientAbiTable` whose exact signatures were already established against the actual ARM64 `libminecraftpe.so` build.

The adapter validates ABI version, protocol, 64-bit pointer width, client context, and all five typed callbacks before attachment.

`ClientAbiTickRunner` then executes the real same-character path:

`client state + inventory + touch -> AI proposal -> existing SharedControlController -> EmbeddedActionFrame -> client SubmitActions`

The existing controller restrictions remain in force, including human gating of ordinary attack/block placement and the explicit emergency-survival exceptions.

The actual Mojang function addresses, signatures, and hook offsets remain build-specific and are still not fabricated. Those must come from the exact authorized 1.26.50 ARM64 client binary before a physical hook installation is attempted.
