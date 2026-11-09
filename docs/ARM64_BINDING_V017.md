# v0.17 ARM64 binding boundary

Target: Minecraft Bedrock 1.26.50 / protocol 2193 on Android arm64-v8a.

## What is now real

The project can inspect the **loaded** `libminecraftpe.so`, validate that it is an ELF64 AArch64 image, obtain its GNU build ID, enumerate executable load segments, parse wildcard byte signatures, require exactly one match, verify the expected bytes at the patch location, reject overlapping patch locations, and hand only verified bindings to a host-supplied hook installer.

The embedded runtime exposes this through `EmbeddedBedrockClientIntegration::bind_verified_hooks(...)`. The installer is deliberately supplied by the modified client because function ABI, trampoline policy, and patch mechanism are properties of the exact target binary.

## Fail-closed rules

No hook is accepted when the library name, architecture, build ID, signature uniqueness, expected bytes, patch range, or executable mapping is invalid. No arbitrary offset table is embedded in the project.

## Exact binary requirement

An exact retail/custom 1.26.50 arm64 `libminecraftpe.so` is required before a real tick/state/input/action signature set can be populated. The repository intentionally contains no fabricated signatures or offsets.

A device running the modified client can call `BedrockAiDeviceProbe.clientImageReport()` to capture the loaded image identity. The report includes the library name, load base, mapped size, ELF class, architecture, GNU build ID, and executable segment ranges.

## Hook specification example

```cpp
ClientHookSpec tick;
tick.name = "client_tick";
tick.kind = ClientHookKind::Tick;
tick.required_build_id_hex = "<exact GNU build id>";
SignaturePattern::parse("<verified bytes with ?? wildcards>", tick.signature);
tick.patch_offset = 0;
tick.patch_size_bytes = 4;
SignaturePattern::parse("<verified original instruction>", tick.expected_bytes);
```

The `ClientHookInstaller` receives the resolved addresses only after every verification condition passes.

## What cannot be truthfully claimed yet

Without the exact target ARM64 library and an Android device/emulator containing it, the project cannot claim a real retail/custom client hook, live client tick interception, or in-world mechanics probe execution. Those are the next physical integration step, not something inferred from a guessed offset.
