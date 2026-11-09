# Logical Replay Format — v0.14

`TransportRecorder` records sanitized logical transport frames only. It does not write socket addresses, authentication credentials, raw JWT tokens, private keys, or arbitrary Android UI data.

The file format is a versioned local binary format. v0.14 writes version 2 and can read the earlier version 1 format:

`BAIRPL01 | u16 version | u32 frame_count | repeated frames`

Each v2 frame stores timestamp, transport-frame format, direction, transport enum, packet ID, sequence, and bounded payload bytes (maximum 4 MiB per recorded frame). Version 1 remains readable; its transport is restored as `Unknown`. Replay files can be loaded for deterministic protocol/runtime diagnostics.

The v0.14 automated interop test generates frames from a local reference harness; this is **synthetic/local provenance**, not a capture from a live 1.26.50 retail server.
