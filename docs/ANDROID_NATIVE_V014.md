# Android Native Boundary — v0.14

v0.14 adds a concrete ARM64/JNI boundary without pretending that the Bedrock client binary has already been modified.

## Native library

`libbedrockai_jni.so` exposes a small stable lifecycle ABI backed by the existing C++ `PortableControllerRuntime`:

- create/destroy an opaque native controller handle;
- configure embedded-client versus relay mode;
- query local-world and remote-server capability decisions.

`BedrockAiNative.kt` is the Kotlin loading/ABI surface. The existing `HostBridge` remains the application-specific seam for the actual Bedrock process hooks.

## Build

The project can be driven through the standard Android NDK CMake toolchain. The build intentionally does not make OpenSSL a requirement on Android; the core crypto API remains provider-based, allowing a later Android host crypto provider to supply P-384/ES384/AES primitives.

No Android NDK is installed in the current build environment, so an ARM64 binary is **not** claimed as locally compiled in v0.14.
