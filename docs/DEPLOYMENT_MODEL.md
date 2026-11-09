# Deployment model

The controller is designed as a **portable client-side controller**, not as a cosmetic
Minecraft asset.

| Package type | Same-character control | Local worlds | Servers without installation |
|---|---:|---:|---:|
| Skin | No | No | No |
| Resource/behavior pack | No | No | No |
| Server plugin/mod | Yes, in principle | Not as a client controller | No |
| Relay/proxy | Yes for supported network paths | No universal local-world path | Yes |
| Embedded client/native module | Yes | Yes | Yes |

The project therefore keeps protocol, AI policy, arbitration and mechanics measurement in
the portable native core. The host layer decides whether that core is embedded in an Android
Bedrock client or connected to a network relay.

A traditional Bedrock skin/pack is deliberately not treated as the deployment vehicle:
the required features depend on controlling touch input, reading client state and creating
serverbound packets. Those are outside what a skin or ordinary pack provides.
