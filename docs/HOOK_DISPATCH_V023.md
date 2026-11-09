# Hook Dispatch ABI — v0.23

## Purpose

v0.23 converts the v0.22 typed dispatcher into an actual registration boundary for the verified five-site hook plan.

The native client remains responsible for the unknown Mojang 1.26.50 ARM64 calling conventions. It must translate those calls into the following controller-owned ABI:

- `on_tick(void*)`
- `on_read_state(void*, const ClientStateView*)`
- `on_read_inventory(void*, const ClientInventoryView*)`
- `on_read_touch(void*, const EmbeddedTouchInput*)`
- `on_submit_actions(void*, const EmbeddedActionFrame*)`

The controller never casts a raw signature address to a guessed Mojang function type.

## Installation order

1. Verify loaded `libminecraftpe.so`.
2. Verify ELF64/AArch64 and exact GNU build ID.
3. Validate all five hook specifications.
4. Resolve every signature uniquely inside executable segments.
5. Validate expected bytes, patch ranges, and overlap.
6. Require the translated hook-dispatch registration callback.
7. Require the exact original Tick identity.
8. Attach the typed ABI table and controller dispatcher.
9. Register all five translated callbacks.
10. Only then call the physical five-site hook installer.

If any step fails, the verified binding set is not installed.

## Detach safety

The integration uses one atomic state word:

- high bit = dispatch disabled;
- lower 31 bits = callbacks currently in flight.

Detach sets the disabled bit before waiting for the lower bits to reach zero. A callback can only increment the counter through CAS while the disabled bit is clear. This closes the check/increment race that a separate `enabled` boolean plus counter would leave.

## Tick path

`on_tick` runs:

`typed client state -> inventory -> touch -> AI -> SharedControlController -> action frame -> typed SubmitActions`

The controller remains same-character: it produces an action frame for the existing client character rather than creating a second player or bot identity.

## Important boundary

v0.23 is not a claim that the retail Bedrock binary has been reverse engineered. The exact target `libminecraftpe.so`, its build ID, and the five native function signatures are still required for physical activation. The registration ABI exists so that those exact signatures can be supplied without contaminating the controller core with guessed binary assumptions.
