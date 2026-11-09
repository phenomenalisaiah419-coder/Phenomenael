# NetherNet/WebRTC Adapter — v0.15

The current adapter follows the transport boundary used by modern NetherNet implementations: a host WebRTC stack owns ICE/DTLS/SCTP negotiation and supplies a reliable data-channel send/receive path.

The C++ `NetherNetTransportBackend` carries the actual Bedrock `0xFE` batch bytes over that reliable channel; it does not add the internal RakNet `TransportFrameCodec` envelope. Incoming data is wrapped back into the existing internal frame seam so the protocol/batch engine remains unchanged.

The Android host queue in `nethernet_host.*` allows a WebRTC data-channel callback to hand received bytes into native code and lets the Android layer drain outbound Bedrock batches.

Full ICE signaling is intentionally external to this module. The adapter does not claim to be a replacement WebRTC stack.
