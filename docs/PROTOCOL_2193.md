# Bedrock 1.26.50 protocol lock

Target: Minecraft Bedrock 1.26.50, stable release, network protocol **2193**.

The preview builds used different protocol numbers. The final 1.26.50 release is 2193; the preview.26 build was 2192. The controller therefore does not accept 2192 as a synonym for 1.26.50.

## Packet IDs used by the current control/state seam

| Packet | ID | Direction | Purpose |
|---|---:|---|---|
| Login | 1 | serverbound | authentication payload (opaque) |
| PlayStatus | 2 | clientbound | status codes |
| ServerToClientHandshake | 3 | clientbound | encryption setup |
| ClientToServerHandshake | 4 | serverbound | encryption setup |
| Disconnect | 5 | both | connection failure/termination |
| StartGame | 11 | clientbound | initial player/world state |
| MovePlayer | 19 | clientbound | authoritative movement/correction |
| InventoryTransaction | 30 | both | item interaction state |
| LevelChunk | 58 | clientbound | world/chunk state |
| NetworkSettings | 143 | clientbound | compression / throttle |
| PlayerAuthInput | 144 | serverbound | player input/state |
| RequestNetworkSettings | 193 | serverbound | advertise protocol version |
| VoxelShapes | 337 | clientbound | collision/shape data before StartGame |

## Endianness notes (1.26.50)

- **RequestNetworkSettings body**: big-endian `int32` protocol version.
- **NetworkSettings body**: little-endian fields (`u16` threshold, `u16` algorithm, `u8` throttle, `u8` throttle threshold, `f32` scalar).
- Packet IDs on the wire are VarUInt32; the session API carries the numeric ID separately from the payload body.

## Transport boundary

The control core accepts decoded packets through `PacketView` / `TransportFrame`. A transport adapter still has to supply authenticated/encrypted Bedrock networking and convert packets into that seam. The current public ecosystem provides 2193-capable implementations, but the controller remains decoupled so transport code can be replaced without rewriting decision logic.

## Authentication / encryption reference notes

For protocol 2193, packet 1 is Login, packet 3 is ServerToClientHandshake and packet 4 is ClientToServerHandshake. Current reference implementations generate a P-384 identity key, use ES384-signed compact JWTs in the login certificate/client-data flow, then derive an encryption key from the ServerToClientHandshake salt and P-384 shared secret. Current GopherTunnel's packet encoder enables AES-256-CTR using the first 12 bytes of the derived key plus a 4-byte counter seed of `...02`, while its checksum covers the per-direction packet counter, packet bytes after the first byte and the derived key. Live server compatibility still requires E2E testing against the exact retail build.
