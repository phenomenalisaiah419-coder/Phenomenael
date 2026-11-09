# Android Native Runtime — v0.15

v0.15 advances the Android boundary from a capability wrapper to a usable native transport/runtime host.

## Native transport lifecycle

`BedrockAiNative.kt` exposes native RakNet connect/poll plus network-loss/network-available callbacks. The C++ host stores the endpoint, closes the active transport on loss, tracks a monotonically increasing network generation, and reconnects through the existing `SocketDatagramIo` + `RakNetTransportBackend` seam after a fresh network becomes available.

The lifecycle coordinator intentionally does not invent credentials after a network transition. A secure login session must be restarted by the host-owned authentication layer when the reconnect invalidates the Bedrock authentication state.

## Android JCA crypto

`AndroidJcaCrypto.kt` uses platform `java.security`/`javax.crypto`: P-384 key generation, SHA384withECDSA, ECDH, SHA-256, plus DER↔JOSE raw `r||s` conversion. `AndroidJcaCryptoProvider` is the JNI bridge implementing the project `BedrockCryptoProvider` interface.

## In-process client boundary

`EmbeddedClientControllerBridge` and the `bedrockai_embedded_*` C ABI are intended for a modified/custom Bedrock client to call from its own tick loop. No second player entity is created. The bridge consumes the existing touch + AI proposal contract and returns the per-channel action frame.

## Current environment limitation

No Android NDK is installed in this environment. CI remains the authoritative ARM64 cross-build check. Host-JNI compilation and JVM execution of the JCA provider are used for local source-level/platform-behaviour validation.
