# v0.16 — Embedded Bedrock 1.26.50 client integration

This stage moves the controller from an exposed JNI tick ABI into a concrete native host contract for a modified Bedrock client process.

## Native contract

The client process provides an `EmbeddedClientHostApi` containing callbacks for:

- real player/world/entity/block state extraction;
- inventory state extraction;
- current touchscreen/button state;
- computing the AI proposal for the current snapshot/inventory;
- submitting the final same-character action frame;
- registering the controller callback at the real client tick site;
- optional loaded-image information.

`EmbeddedBedrockClientIntegration` validates ABI/protocol/game/library identity before attaching, converts the client state into the existing `WorldSnapshot`/`InventoryState`, requests the AI proposal from the host-side AI callback, invokes the unchanged shared-control arbiter, and submits the final action frame back into the same player. Ordinary AI attack/place remain human-gated; emergency placement/utilities are only possible when the existing survival policy explicitly marks the proposal as emergency-allowed.

## Android process boundary

The native library now exposes the integration as a JNI/native object and a C-entrypoint contract for native client/preloader code. The contract uses the supplied C++ header and matching native toolchain; it is not presented as a language-neutral FFI ABI. Android Java is only a lifecycle surface; real state/input/action callbacks remain native.

The loaded image is expected to be `libminecraftpe.so`, ARM64, and the host contract is pinned to Bedrock 1.26.50 / protocol 2193.

## Version-specific hook reality

A literal function-address or byte-signature hook into Mojang's stripped 1.26.50 ARM64 `libminecraftpe.so` cannot be responsibly hard-coded without the exact target binary. Current native-mod tooling uses version-specific signatures/offsets and requires verifying those signatures against the exact library before hooking. This repository therefore provides the concrete callback/hook ABI and image-validation layer, but does **not** invent unverified addresses.

Public native-mod references confirm that `libminecraftpe.so` is the relevant Android ARM64 game library and that version-specific signatures/offsets must be updated and verified per game build. citeturn895876search0turn895876search1

## Safety preservation

The integration does not add any bed-destruction action. Human-held attack/place channels still gate ordinary attack/place behavior, while the existing emergency survival policy remains the only autonomous exception.

## Current verification boundary

The callback ABI is unit/integration tested on desktop and compiled as part of the Android JNI target. Real ARM64 linking and live attachment to `libminecraftpe.so` require the exact APK/native library and an Android NDK/device or emulator.
