# RakNet v0.11 Transport Notes

## Data-path ownership

`SocketDatagramIo` owns the native UDP socket.

`RakNetConnection` owns the offline handshake phases:

```text
Idle
 → OpenConnection1Sent
 → OpenConnection2Sent
 → ConnectionRequestSent
 → Connected
```

`RakNetReliabilityEngine` owns online datagram sequencing, frame parsing, delivery ordering, ACK/NACK state and reliable retransmission.

`RakNetTransportBackend` composes the two and remains behind the existing `BedrockTransportBackend` interface.

## Offline handshake

Client:

```text
OpenConnectionRequest1
OpenConnectionRequest2
ReliableOrdered(ConnectionRequest)
```

Server:

```text
OpenConnectionReply1
OpenConnectionReply2
ReliableOrdered(ConnectionRequestAccepted)
```

After acceptance, the client emits `ReliableOrdered(NewIncomingConnection)` and `Unreliable(ConnectedPing)` through connected frame sets.

## Connected frame format supported

The implementation handles the RakNet frame fields used by the scoped reliability profile:

- frame reliability bits;
- split flag;
- 16-bit bit-length;
- 24-bit reliable index when reliable;
- 24-bit sequencing index when sequenced;
- 24-bit order index + channel when ordered;
- split count, split ID and split index;
- payload body.

Datagram sequence numbers and reliable/ordered indexes are kept as 24-bit values with wrap-aware comparisons.

## Reliability policy

The controller uses reliable-ordered channel 0 for Bedrock control/game payloads by default.

Implemented safeguards:

- bounded reliable window;
- bounded outbound queue;
- bounded receive-gap window;
- duplicate reliable-frame suppression;
- ACK generation for received frame-set datagrams;
- NACK generation for detected sequence gaps;
- explicit NACK-triggered retransmission;
- timer-triggered retransmission;
- terminal state after the configured retry limit;
- fragmentation/reassembly limits and expiry;
- ACK/NACK packet construction constrained to the negotiated MTU.

## Socket path

`SocketDatagramIo` uses a connected UDP socket and non-blocking `send`/`recv`.

The peer and local socket addresses are exposed in RakNet protocol-address encoding so `NewIncomingConnection` can be constructed without inventing a host address for resolved endpoints.

The implementation has platform branches for POSIX and Windows and uses only native UDP/system APIs, making the source suitable for an Android NDK host bridge. The current CI/build environment does not include the Android NDK, so Android compilation itself remains an unverified platform check.

## Verification boundary

v0.11 has two kinds of network evidence:

1. Pure loopback unit tests for deterministic reliability and handshake behavior.
2. A real localhost UDP integration test using the concrete socket class and a fake Bedrock-like server sequence.

That evidence does not substitute for a real Minecraft server. Authentication, encryption and game-login glue are intentionally the next stage.
