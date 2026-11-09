# Secure Session — v0.13

## Wire order

```text
RakNet connected
    ↓
RequestNetworkSettings (0xFE batch, plaintext)
    ↓
NetworkSettings (0xFE batch, plaintext)
    ↓
Login (0xFE batch, negotiated compression, plaintext)
    ↓
ServerToClientHandshake (0xFE batch, negotiated compression, plaintext)
    ↓
verify ES384 JWT / x5u P-384 / 16-byte salt
    ↓
ECDH(client private, server public) → 48-byte X coordinate
    ↓
SHA-256(salt || sharedSecretX) → 32-byte AES key
    ↓
enable AES-256-CTR
    ↓
ClientToServerHandshake (0xFE batch, encrypted)
    ↓
subsequent 0xFE batches (encrypted, optionally compressed)
```

The packet cipher leaves the first 0xFE byte plaintext and encrypts the remaining batch bytes after appending the 8-byte SHA-256 checksum/counter construction. The secure runtime switches the cipher only after the server JWT signature and key material have been validated.

Authentication material remains host-owned. No Microsoft/Xbox credential is manufactured by this core.
