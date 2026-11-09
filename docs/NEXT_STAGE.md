# Next Stage

1. Run the v0.27 Android secure-login path with a cache created for the same host:port. The native cache validation and document-picker import helpers are integrated; the host application still needs to wire its actual Storage Access Framework UI and background poll loop.
2. Run the updated Microsoft → Xbox device auth → SISU → multiplayer XSTS path on the user's Termux device and capture the failing stage plus XErr if any. No tokens or cache files should be shared.
3. Validate RakNet Reply1 cookie negotiation, ConnectionRequestAccepted, prebuilt Login, and the Bedrock encryption handshake against a controlled Bedrock 1.26.50 server, then test a real target server with user authorization.
4. Supply the exact authorized 1.26.50 ARM64 `libminecraftpe.so` and derive the five native hook ABIs from that build.
5. Implement native translator wrappers for real Tick, ReadState, ReadInventory, ReadTouch, and SubmitActions signatures and emit the v0.25 coherent event sequence.
6. Measure Android tick latency, event ordering, re-entry, detach races, and state/action reconciliation on hardware.
7. Enable physical in-client hook installation only after the exact build ID, signatures, and calling conventions are independently verified.

No production offsets, signatures, native object layouts, or Mojang calling conventions are invented. Live Microsoft/Xbox and Bedrock-server tests remain environment-dependent.


## v0.27 RakNet follow-up

- RakNet's server-security cookie challenge is supported and tested.
- The native API preserves stage-specific timeout/reliability errors for `nativeLastError()`.
- Do not set the vanilla client's legacy RakNet encryption flag to true: Bedrock documents the Request2 client security-support field as false and then negotiates Minecraft-layer encryption after Login. See `RAKNET_SECURITY_TIMEOUT_V027.md`.
- If the native error is `waiting for OpenConnectionReply1`, capture only non-secret facts (hostname/IP category, port, network type, timestamp, error string), and check whether the server is reachable over UDP. Do not share the login cache or any token/JWT/private-key data.


## v0.28 → v0.29 gameplay intelligence integration

1. Implement the raw 1.26.50 / protocol 2193 clientbound play-packet decoders needed to populate player vitals, entities/opponent state, inventory/hotbar, equipment/armor, effects, blocks and server corrections. Keep unknown values explicitly unknown.
2. Extend the version-pinned `PlayerAuthInput` codec for the 1.26.50 `ItemUseInventoryTransaction` field order and implement the item-stack-request, block-action and equipment/slot packets required by actions. The existing codec currently fails closed when optional action payloads are present.
3. Add a session decision loop: update observations → decide once → encode a legal packet/action → await server state → verify outcome. Prevent duplicate item uses while a prior action is unresolved.
4. Wire all observed success/failure signals into `SkillExperienceBook`; save outcome memory to private Termux storage with bounded file input and crash-safe writes.
5. Build replay fixtures for inventory, health, effects, entity moves, corrections and target visibility, then test every action encoder before a live practice-server attempt.
6. Measure Bedrock/server-specific melee reach, attack cadence, arrow/pearl/wind-charge/fireball/potion physics, block placement constraints and latency response in a permitted practice environment. Leave calibration flags false until measured.
7. Complete and validate individual executors (food, potion, weapon switching, attack with held item, bow, pearl, wind charge, water/block clutch, mine, bridge, sprint/jump) against server-confirmed results.

Do not call the controller a fully autonomous PvP bot until these live observation → decision → action → feedback loops are verified.


## Completed in v0.32: registry and attributes

- Implemented protocol-2193 `ItemRegistry` (162), including its per-entry Network-Little-Endian NBT field, bounded parsing, collision-safe numeric-ID lookup and retroactive resolution of observed inventory/equipment/container items.
- Implemented `UpdateAttributes` (29), retaining bounded actor attribute records and synchronizing recognized player vitals/server ticks.
- Verified with strict GCC Release build and CTest 32/32; strict Clang builds of the v0.30/v0.31/v0.32 decoder test targets; v0.32 ASan/UBSan with leak detection. No real server capture or physical device test was performed.

## Remaining item/world observation work

1. Decode ItemRegistry NBT/component values semantically only after each supported component schema is established with protocol references and known-good examples. Preserve unknown tags and custom Hive IDs; do not infer capabilities from names.
2. Finish entity metadata, inventory/component edge cases, chunk/block updates, collision, raycast/visibility and placement checks. Unknown or unloaded blocks must not be treated as air.
3. Extend the test corpus with authorized, secret-free protocol-2193 captures and property/fuzz tests. Keep malformed input atomic and maintain strict memory bounds.

## v0.32 → v0.33: outbound action protocol and one verified action slice

1. Verify protocol-2193 `PlayerAuthInput` and all optional interaction fields against the pinned schema; preserve correct sub-packet ordering, flags and tick fields, and reject action payloads the host cannot fully encode.
2. Implement the smallest end-to-end action vertical slice (movement/rotation first) with a version-pinned outbound packet encoder, sequence/tick validation, finite/range checks and a deterministic byte-level fixture derived independently from the schema.
3. Connect the encoder to the existing session send path only through the host-owned action boundary. Keep this slice movement-only; do not fabricate item-use, inventory-request or block-action fields.
4. Add tests that prove exact encoded bytes, invalid inputs are rejected without send, packets are queued/sent once, and no duplicate action is emitted while a prior action awaits state feedback.
5. Then implement `ItemStackRequest`, `ItemUseInventoryTransaction`, and block action payloads as separately tested vertical slices. Use server responses and inventory/position changes for reconciliation; a successful enqueue is not proof that an action succeeded.
6. Before real gameplay testing, use a controlled, authorized protocol-2193 server and secret-free captures. Test the initial movement slice on a real Termux/Android device before adding attacks or item usage.

Remaining observation work in parallel milestones includes entity metadata, chunks/block updates, collision and raycast/placement knowledge, and semantic component decoding. Unknown blocks are not air; unknown item capabilities are not permission to use an item. Do not claim full controller completion until movement plus item/block actions are implemented, integrated with the planner, and verified by server-confirmed state feedback.



## v0.33 movement action slice

- Implemented version-pinned movement/rotation `PlayerAuthInput` send path via `MovementActionSender` and the existing `Protocol2193NetworkEngine` batch path.
- Enforces in-game state, finite/range-limited movement and camera fields, increasing ticks, empty batch boundary, no optional interaction fields, one pending action, and server-tick feedback before the next action.
- If a transport flush fails ambiguously, the action remains pending and is not blindly retried. Recovery policy must be informed by reconnect/session state rather than replaying the action.
- Verified with deterministic byte-level decode/send tests. No physical Android/Termux or live server test was performed.

## Next action slice

1. Add a server-confirmed movement reconciliation path that connects decoded authoritative server ticks/corrections to `MovementActionSender::observe_server_tick`.
2. Exercise this movement-only action on a controlled authorized protocol-2193 server and real Termux device.
3. Only then add the `ItemStackRequest`/`ItemUseInventoryTransaction` and block-action codecs as independent, strictly validated slices.


## v0.34 movement feedback integration

- Added `local_player_input_tick` to per-frame decode reports; this metadata is cleared each frame and is emitted only by supported local-player tick-bearing packets when the tick is non-zero.
- Added `MovementFeedbackBridge`, an explicit host seam connecting successful observation decoding to `MovementActionSender::observe_server_tick`.
- Test coverage proves that non-local actor ticks cannot acknowledge a pending action, ticks below the submitted tick do not release the action, a matching player correction does, and malformed feedback fails closed.
- GCC 14 strict Release all-target build and full CTest suite passed: **34/34**.

### Next required work

1. Wire `MovementFeedbackBridge::process_frame` into the actual active session's clientbound receive path; do not separately feed the same frame twice to the observation decoder.
2. On an authorized controlled server and physical Termux/Android device, verify sent PlayerAuthInput, echoed input ticks, server corrections, and disconnect/reconnect behavior. A valid outbound write alone is not gameplay success.
3. Add the separately tested item-stack-request, item-use-transaction and block-action vertical slices only after movement feedback passes live testing.
4. Keep Hive/custom-server item actions and movement physics uncalibrated until observed and measured for that server/mode.
