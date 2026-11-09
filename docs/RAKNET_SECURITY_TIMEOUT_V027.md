# RakNet Security and Timeout Fix — v0.27

Target profile: Minecraft Bedrock 1.26.50 / protocol 2193, RakNet wire protocol 11.

## What is now implemented

1. **Monotonic elapsed-time handling.** The offline handshake no longer expires after a fixed number of calls to `poll()`. It uses a wall-clock handshake deadline and retry interval, so a fast Android frame loop does not burn the timeout.
2. **Stage-specific timeout errors.** Failure names the stage currently waiting for a response: `OpenConnectionReply1`, `OpenConnectionReply2`, or `ConnectionRequestAccepted`. The Android native poll path retains that precise diagnostic even when called without an error-output buffer.
3. **MTU fallback.** On each Request1 retry interval, the padded probe MTU steps down to the configured minimum. This helps when a large initial UDP datagram is dropped or fragmented by the network path.
4. **Reply1 security-cookie support.** If the server sets the Reply1 `ServerHasSecurity` flag, the client parses the following 32-bit cookie and echoes it plus the vanilla-compatible support byte in Request2. The Request2 retry resends the same negotiated values.
5. **Android secure-login route.** The native controller can validate an app-private prebuilt Login cache, attach `Bedrock2193SecureRuntime`, and drive the authenticated connection. The Termux exporter excludes refresh tokens and Xbox device-token files from the Android import bundle.

## Important protocol distinction

“RakNet security” can refer to two distinct features:

- **The Reply1 security-cookie challenge** is anti-DoS negotiation. v0.27 supports and tests this path.
- **Legacy RakNet/Libcat packet encryption** is a different feature. The vanilla Bedrock-compatible Request2 support field is false; OpenConnectionReply2's encryption flag is also expected to be false in this profile. Bedrock's actual session encryption is negotiated at the Minecraft protocol layer after Login, using `ServerToClientHandshake` and the P-384 identity key.

Therefore v0.27 does not falsely set the legacy RakNet security-support byte to `1`. That byte is not a generic security toggle; changing it without implementing the matching Libcat key exchange, proofs and packet cipher would break interoperability rather than harden the connection.

Protocol references:

- Bedrock Wiki RakNet handshake documentation: https://wiki.bedrock.dev/servers/raknet
- Bedrock-OSS maintained RakNet page: https://github.com/Bedrock-OSS/bedrock-wiki/blob/wiki/docs/servers/raknet.md

## Reading a timeout

| Error | Likely meaning | Next diagnostic step |
|---|---|---|
| `RakNet handshake timeout: waiting for OpenConnectionReply1` | No usable Reply1 arrived after the MTU probe/retries | Verify the exact host/UDP port, server reachability, mobile network UDP restrictions, and server protocol/version |
| `RakNet handshake timeout: waiting for OpenConnectionReply2` | Reply1 arrived but Request2 did not complete | Check server cookie/address response and UDP filtering |
| `RakNet handshake timeout: waiting for ConnectionRequestAccepted` | Offline negotiation completed but the reliable ConnectionRequest did not receive acceptance | Check server-side connection policy, reliable retransmission logs, and whether the endpoint is a Bedrock listener |
| `RakNet reliable datagram exceeded retry limit` | A reliable datagram was not acknowledged within the configured retry policy | Treat as packet loss/server non-response; preserve this diagnostic instead of reducing it to a generic timeout |
| `OpenConnectionReply2 ... encryption-enabled server is outside the Bedrock profile` | Peer requested legacy RakNet encryption, which this vanilla Bedrock profile deliberately does not negotiate | Confirm the port belongs to a vanilla-compatible Bedrock listener; do not enable a bare boolean as a workaround |

## Test coverage and limits

The v0.27 tests cover Reply1 cookie parsing and Request2 echo, stage-specific timeout behavior, poll-frequency independence, preservation of the diagnostic without an output buffer, MTU reduction, and the existing authentication/cache contracts. They are local deterministic tests, not proof of a live Microsoft sign-in or real-server login. A real session still needs to be tried on the target Android network/server, and any failure report must omit tokens, JWTs, private keys, and the cache archive.
