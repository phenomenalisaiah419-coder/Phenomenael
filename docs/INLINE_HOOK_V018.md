# v0.18 — ARM64 inline Tick hook runtime

The v0.18 runtime is the first source stage that can physically modify the loaded Bedrock client text when the exact verified 1.26.50 ARM64 image and hook profile are available.

## Patch model

Only the first 32-bit instruction of the verified Tick function is replaced. The patch is an AArch64 `B` to a nearby executable veneer. Keeping the patch to one aligned instruction avoids the partial 16-byte write hazard of a multi-instruction entry patch on a running multicore process.

The veneer loads a per-hook context and enters `bedrockai_arm64_tick_entry`. The universal entry stub saves x0-x30, q0-q7 and NZCV, calls the existing embedded AI tick bridge, restores the saved state, and branches to an executable trampoline.

The trampoline contains a BTI landing instruction, the original first instruction, and an absolute branch back to `target+4`.

## Fail-closed conditions

Installation refuses to proceed for a missing build ID, non-unique signature, expected-byte mismatch, non-executable target, unaligned target, unsupported first instruction, out-of-range veneer, invalid memory protection transition, or a non-AArch64 live process.

The current conservative relocator rejects PC-relative control/data instructions, indirect branches and returns as the first relocated instruction. It does not invent relocation rules for instruction classes it cannot prove safe.

## Lifetime

Installed hooks are process-lifetime patches in this stage. `EmbeddedBedrockClientIntegration::detach()` atomically disables the dispatch callback and waits for in-flight dispatch calls, but does not unpatch executable text or free the trampoline. This avoids use-after-free when a game thread is still executing the generated trampoline. Reattachment after a live Tick hook is intentionally rejected.

## Physical activation

No Mojang-specific addresses or signatures are supplied by this stage. The exact `libminecraftpe.so` must be fingerprinted on the target Android build and the verified Tick instruction must be entered into the hook profile before installation can succeed.
