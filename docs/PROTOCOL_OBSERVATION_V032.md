# Protocol observation decoder v0.32

Target: Minecraft Bedrock 1.26.50, protocol 2193. This document describes the new handlers added in v0.32. The decoder only receives `TransportFrame` objects after transport framing and the secure-session layer; packet identifiers here are Bedrock packet IDs, not RakNet packet IDs.

## Added packet: ItemRegistry (162)

The decoder reads the protocol-2193 ItemRegistry entry sequence with bounded, versioned fields:

| Field | Decoder handling |
|---|---|
| Entry count | Varuint32, maximum 8,192 entries |
| Item name/identifier | Length-prefixed protocol string, non-empty and bounded |
| Numeric item ID | Signed little-endian 16-bit ID |
| Component-based flag | Boolean byte; values other than 0 or 1 are malformed |
| Version | ZigZag signed VarInt32 |
| Item Component Data | Network-little-endian NBT compound for each entry, preserved as raw bytes after structural validation; non-component entries commonly have an empty compound |

NBT is read for every registry item entry rather than conditionally skipped based on `Is Component Based`; skipping it would desynchronize the remainder of the registry array. NBT validation is deliberately structural rather than semantic. It accepts the supported base NBT tag types, with bounded nesting, node count, collection lengths, per-entry NBT bytes and aggregate registry NBT bytes. It does not decode component schemas into item abilities. Truncated, malformed, oversized or trailing bytes cause rejection before registry state is committed.

All parsed registry entries are retained in observation order. A numeric-ID lookup is exposed only for unambiguous mappings. Same-ID collisions with conflicting identifiers or metadata are recorded as ambiguous; item stacks depending on those IDs are left unidentified, including when a later registry update invalidates a previously resolved mapping. Registry updates re-resolve already-observed inventories, armor, held items and container slots.

A small explicit allowlist maps familiar `minecraft:` identifiers to existing high-level `ItemKind` values. It is not a full vanilla catalog. Custom namespaces (including Hive IDs) can have their identity recorded but remain `ItemKind::Unknown` unless a separate verified item profile provides capabilities and activation behavior. Names, display labels and raw component NBT never grant use permission.

## Added packet: UpdateAttributes (29)

The decoder reads the target actor runtime ID, bounded attribute array, attribute min/max/current and default min/max/value floats, attribute name, bounded modifier array, modifier identifiers/names, finite modifier amounts, operation/operand integers, serializable flag, and trailing varuint64 player-input tick.

Numeric fields must decode as finite values; min/max bounds are validated. Attribute count is capped at 256 and modifiers per attribute at 128. Unknown actors can produce a structurally valid `PartiallyApplied` result without fabricating an entity. Fully malformed, truncated, over-limit or trailing-byte packets are rejected before attributes, vitals or server tick are committed.

Attributes are retained on the local player or tracked entity. Recognized local-player names synchronize the existing observation fields for `minecraft:health` (current and maximum health), `minecraft:absorption`, hunger attributes and `minecraft:armor`. The wire data remains available in the full attribute record with modifiers and server observation tick; unknown attributes are preserved rather than discarded.

## Tests and boundaries

`tests/test_v32.cpp` covers registry-to-inventory resolution before and after packets arrive, a known vanilla item classification, custom item identity without invented capabilities, conflicting numeric-ID collisions, stale-identity clearing, raw NBT retention, malformed NBT, local vitals/modifiers/server tick and malformed/oversized packets. Each strict prefix of valid registry and attribute test fixtures is rejected with state unchanged.

This packet decoder is not a complete world or action pipeline. Still out of scope: semantic ItemStack component interpretation, exhaustive item mapping, entity metadata, chunks/block/collision state, outbound PlayerAuthInput interaction fields, inventory stack requests, item-use transactions, block actions, live server reconciliation and autonomous gameplay. No live Hive/SMP or physical Termux test was performed for this release.

## Version-pinned schema references

- [Mojang Bedrock 1.26.50 — ItemRegistry packet](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/item-registry-packet/)
- [Mojang Bedrock 1.26.50 — UpdateAttributes packet](https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/update-attributes-packet/)
- [Independent implementation reference: Gophertunnel ItemEntry NBT serialization](https://github.com/Sandertv/gophertunnel/blob/master/minecraft/protocol/item.go)
- [v0.31 inventory/equipment decoding](PROTOCOL_OBSERVATION_V031.md)
- [v0.30 base world/player/entity observation](PROTOCOL_OBSERVATION_V030.md)
