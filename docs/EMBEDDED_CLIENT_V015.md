# Embedded Client Process Boundary — v0.15

The intended production deployment is still a modified/custom Android Bedrock client that carries the controller into local worlds, LAN worlds, Realms, and remote servers.

The new `bedrockai_embedded_*` ABI is deliberately independent of UI automation. The host client owns the real Bedrock player object, game tick, touch state, inventory/economy state, and packet plumbing; the controller consumes a bounded snapshot/input proposal and returns a bounded action frame for the same player.

This stage does not claim that Mojang's retail APK binary has been patched. Client-specific symbol/offset integration remains build-specific and must be performed in the custom client source tree.
