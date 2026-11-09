# Portable Android deployment target

## What this project is

The final target is **client-side software controlling the same Bedrock player character**.
It is not a skin, and it is not a server-side plugin.

For Android, the strongest end-state is a Bedrock client build/APK containing the native
controller core (JNI/native bridge). That lets the controller travel with the player into
local worlds, LAN worlds, Realms and network servers without asking each server owner to
install anything.

A relay/proxy remains useful for network-reachable servers, but it is not the universal
solution for local worlds. A resource/behavior pack alone is also insufficient because it
cannot provide the required arbitrary input arbitration, packet control, or autonomous
same-character movement.

## Intended modes

- EmbeddedClient: production target for portable same-character control.
- RelayProxy: network-server compatibility path.
- CompanionOverlay: experimental Android helper only; it does not replace embedded
  integration for local-world control.

All modes are pinned to Bedrock 1.26.50 / protocol 2193 in this development branch.
