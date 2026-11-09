# Android device probe — v0.17

The embedded native layer now exposes a read-only client image report before any hook installation is attempted.

Kotlin:

```kotlin
val report = BedrockAiDeviceProbe.clientImageReport()
android.util.Log.i("BedrockAI", report)
```

The native report is intentionally diagnostic only. It does not patch memory, modify the client, or accept unverified addresses.

Required identity for this release:
- `library=libminecraftpe.so`
- `elf_class=64`
- `machine=183` (AArch64)
- non-empty GNU build ID
- at least one executable segment
- Minecraft Bedrock `1.26.50` / protocol `2193` at the host ABI boundary
