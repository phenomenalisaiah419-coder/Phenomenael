# v0.12 Authentication/Login Layer

Target: Minecraft Bedrock 1.26.50 / protocol 2193.

Implemented:
- P-384 identity-key generation through an injected crypto provider.
- Exact Login packet payload envelope: big-endian protocol version followed by a VarUInt32-length ConnectionRequest.
- Modern certificate-chain construction matching the current reference flow: prepend a client-signed ES384 JWT with `x5u` and `identityPublicKey`, preserve the supplied authentication-service chain, then append a client-signed ClientData JWT.
- Strict multiplayer-token `cpk` validation against the generated identity public key.
- No fabricated Microsoft/Xbox credentials.
- Host-owned `BedrockLoginAuthProvider` boundary so the Android/application layer supplies genuine authentication-service material.
- OpenSSL desktop/CI provider for P-384, ES384, ECDH and SHA-256.

Important protocol boundary:
- The server Login request and connection request are plaintext at this stage.
- Token acquisition and account authentication remain external because credentials must come from the platform/auth service rather than being hard-coded in this core.
- Encryption handshake and the connected RakNet → login/bootstrap → encrypted batch glue are next.

The current Bedrock 1.26.50 protocol reference identifies Login as packet 1 and ServerToClientHandshake as packet 3; current GopherTunnel reference code constructs the certificate chain and uses the generated identity key for client data signing.
