# Local Bedrock Interoperability Harness — v0.14

`tests/interoperability/local_bedrock_server.*` implements a deterministic UDP peer for protocol 2193.

The test drives the actual production client path:

`SocketDatagramIo → RakNet offline handshake → reliable ConnectionRequest → ConnectionRequestAccepted → Bedrock 0xFE batch → NetworkSettings → Login → ServerToClientHandshake → P-384 ECDH → AES-256-CTR → ClientToServerHandshake → encrypted StartGame`.

The harness is a compatibility probe, not a replacement for a live retail Bedrock server. It does not fabricate Microsoft/Xbox credentials and it is not used as evidence of retail-server compatibility.
