# Native ABI Certificate — v0.24

## Purpose

v0.23 established a typed five-hook dispatch boundary. v0.24 makes the native side machine-checkable before physical installation. The controller still does not guess Mojang private signatures or object layouts.

## Certificate

`ClientNativeAbiCertificate` contains:

- certificate version
- protocol (2193)
- 64-bit pointer-width requirement
- exact loaded GNU build ID
- one verified hook target address for each of Tick, ReadState, ReadInventory, ReadTouch, and SubmitActions
- one native translator address for each of those five hook sites

The validator rejects:

- certificate version/protocol/pointer-width mismatch
- malformed or mismatched build IDs
- hook targets that do not exactly equal the already-verified binding patch addresses
- missing translators
- translators outside executable `libminecraftpe.so` ranges
- duplicate translator addresses

## Installation order

1. Inspect and validate `libminecraftpe.so`.
2. Resolve all five hook signatures and expected bytes.
3. Build the five verified bindings.
4. Read and validate the native ABI certificate.
5. Build and register the stable typed dispatch callbacks.
6. Ask the modified client to install all five bindings transactionally.

Failure at any stage leaves physical hook installation disabled.

## ABI versioning

Adding the certificate reader changes `EmbeddedClientHostApi`, so `kEmbeddedClientAbiVersion` is now **2**. An old host using version 1 is rejected rather than being interpreted as a shorter structure.

## Still intentionally unverified

The repository does not contain a production 1.26.50 `libminecraftpe.so`, so it does not contain production signatures, offsets, native class layouts, or calling conventions. Those must be derived from the exact authorized binary before real Android installation.
