# v0.35 notice

This package adds the shared-control intelligence layer on top of v0.34: second-input takeover, predictive survival, contextual skill learning, enemy and item
learning, a fair-play gate, a 166-entry honest skill catalogue and a closed-loop simulator. Read `docs/AUDIT_V035.md` first (what is verified, what is not).
Build without CMake: `make -j2 check` (add `OPENSSL_LIB=-lcrypto` on Termux after `pkg install openssl`). Nothing here has been run on a phone or a live server.

The optional Android companion project is in `companion/`. Stage 1 provides user-started screen capture, manual test gestures and a STOP overlay for the existing Minecraft app. It is not an embedded client or an autonomous controller; its native safety check uses the v0.35 `ControlHub` with an unknown server classification and must remain observe-only. See `companion/README_STAGE1.md`.

---

# Bedrock AI Controller — v0.34

Target: Minecraft Bedrock 1.26.50 / protocol 2193.

## Deployment target

Client-side software controlling the **same** player character. It is not a skin and not a server-side plugin. The production target remains an embedded/custom Bedrock Android client that can be carried into different worlds and servers.

## v0.17 — exact ARM64 binding machinery + Android device probe (historical)

v0.16 preserves v0.1–v0.15 and adds the concrete native in-process client contract: version-pinned client identity checks, real state/inventory/touch extraction callbacks, AI decision callbacks, same-character action submission, tick registration, and loaded `libminecraftpe.so` image validation.


### New in v0.17
- ELF64/AArch64 loaded-image fingerprinting with GNU build ID and executable-segment enumeration.
- Wildcard ARM64 signature parsing and executable-only scanning.
- Unique-match, build-ID, expected-byte, patch-range and overlap verification.
- Verified hook installer boundary integrated into `EmbeddedBedrockClientIntegration`.
- Android device probe for capturing the exact loaded `libminecraftpe.so` identity before hooks are installed.
- No fabricated offsets or signatures.

### Preserved controller contract
- One character only.
- AI never destroys beds; no bed-destruction action exists in the AI action set.
- Normal AI attack requires human attack input.
- Normal AI placement requires human place input.
- Emergency survival may use explicitly authorized placement/utilities.
- Human-owned shop/buying/inventory/economy actions remain outside AI authority.
- Protocol-specific reach, cadence, placement and movement values are obtained by measurement rather than Java-only assumptions.

### New in v0.16
- Version-pinned `EmbeddedBedrockClientIntegration` for Bedrock 1.26.50 / protocol 2193.
- Real native callbacks for client state, inventory, touch input, AI decisions, action submission and tick registration.
- Full state mapping for player, entities, blocks, dimension and server correction into the existing controller core.
- Loaded `libminecraftpe.so` image discovery/validation on Android/Linux.
- Native C-entrypoint contract for a modified Bedrock client or preloader to attach at the real tick/input/state sites; the supplied headers define the required native ABI.
- Global no-warning C++ policy, including conversion/sign-conversion/shadow diagnostics as errors.
- Repository-wide GCC and Clang warning-clean verification; all warnings encountered during the v0.15 audit were fixed rather than suppressed.

### Explicit limitations
1. No Microsoft/Xbox credentials are minted by the project.
2. Full ICE/DTLS/SCTP signaling is owned by the Android WebRTC host; the C++ NetherNet layer is the transport adapter.
3. No Android NDK is installed in the current environment, so ARM64 binary linking is CI-verified rather than locally linked.
4. A real retail Bedrock server E2E run is not claimed without legitimate external credentials.
5. Client-specific Bedrock symbol/offset integration remains custom-client source work.
6. The current environment has no Android NDK, so the actual ARM64 link remains CI-only; the JNI/native source boundary has been compiled with Android-simulated headers and strict diagnostics.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

All C++ targets build with warnings treated as errors.


## v0.17 final verification (historical)
- GCC Release + strict warning policy: 15/15 tests passed.
- Clang Release + strict warning policy: 15/15 tests passed.
- ASan/UBSan + leak detection: 15/15 tests passed.
- Kotlin/JNI device-probe boundary: compiled with strict diagnostics.
- No fabricated ARM64 offsets/signatures. Exact hook binding remains fail-closed until the target 1.26.50 ARM64 library is supplied.


## v0.18 historical additions
- Conservative live AArch64 inline Tick hook runtime using a single-instruction `B` patch.
- Nearby executable veneer allocation within the AArch64 branch range.
- Universal ARM64 Tick entry stub preserving x0-x30, q0-q7 and NZCV before dispatching the controller.
- Executable trampoline that runs the original first instruction and resumes at `target+4`.
- Exact original-byte revalidation, executable-range checks, instruction relocation guards, page-protection preflight/restore and instruction-cache flushing.
- In-flight dispatch guard so disabling a live hook cannot call back into a destroyed integration object.
- JNI API accepts an exact build ID, signature and expected instruction and routes them through the same fail-closed verifier.
- Physical hook activation is still intentionally blocked when the target ARM64 `libminecraftpe.so` is absent or does not match the verified build.

## v0.18 historical verification
- GCC Release strict suite: 16/16 passed.
- Clang 17 Release strict suite: 16/16 passed.
- GCC ThreadSanitizer suite: 16/16 passed.
- ASan/UBSan with leak detection: 16/16 passed.

## v0.20 additions
- Exact current-process runtime preparation for the already-loaded `libminecraftpe.so`.
- Optional exact GNU build-ID requirement before the embedded runtime becomes prepared.
- Process ID and image report exposure through Android JNI.
- Global exported-symbol resolution guarded by the verified executable ranges of the target client image.
- Machine-readable seven-field hook-plan parser/validator for Tick/state/inventory/touch/action sites.
- Single-build-ID enforcement across the whole hook plan and mandatory Tick hook presence.
- No second arbitrary copy of `libminecraftpe.so` is loaded by the preparation path.

## v0.20 verification
- GCC Release + all warnings as errors: 18/18 tests passed.
- Clang 17 Release + all warnings as errors: 18/18 tests passed.
- ASan/UBSan v0.20 target with leak detection: passed.
- JNI strict syntax compile: passed.
- Kotlin declaration compile: passed.
- v0.18 → v0.20 source preservation audit: 0 missing files.
- No warning suppression added.

See `docs/PROCESS_RUNTIME_V019.md` for the exact client-process runtime contract.


## v0.20 complete hook-plan binding
The integration validates one exact five-site plan (`Tick`, `ReadState`, `ReadInventory`, `ReadTouch`, `SubmitActions`) against the loaded build ID and executable segments, then passes the complete verified binding set to the modified client’s `install_client_hooks` callback. The callback is where the exact ABI-specific native hooks are installed; this project deliberately does not guess Mojang function signatures or offsets.


## v0.22 — typed ARM64 client ABI adapters

v0.22 adds the real typed native ABI seam behind the verified five-hook plan. The core never casts an arbitrary address into a guessed Mojang function type. The exact modified-client integration supplies the verified callback table after it has established the actual 1.26.50 ARM64 signatures and calling convention.

The typed flow is:

`client state + inventory + touch -> AI proposal -> SharedControlController -> action frame -> client SubmitActions`

Added:
- `ClientAbiTable` with typed Tick, State, Inventory, Touch and SubmitActions callbacks.
- ABI version, protocol, pointer-width, context and callback validation.
- Immutable table identity checking during a tick.
- `ClientAbiTickRunner` executing the same-character controller path through those typed callbacks.
- Regression tests for invalid ABI metadata, callback failure propagation, human Attack gating and table mutation.

This stage does not invent or claim Mojang's actual function signatures or offsets. Those remain exact-binary data supplied by the authorized 1.26.50 ARM64 client integration.

## v0.22 verification
- GCC strict Release: 19/19 tests passed.
- Clang strict Release: 19/19 tests passed.
- ASan/UBSan v0.22 target: passed.
- GCC ThreadSanitizer v0.22 target: passed.
- No compiler-warning suppression added.


## v0.22

Adds the typed five-site runtime hook dispatcher behind the verified client ABI: Tick, state, inventory, touch, and action submission. The dispatcher is fail-closed and does not invent Mojang function signatures.


## v0.24 — registered five-site hook dispatch

v0.24 connects the typed dispatcher to the complete verified hook-plan installation path. A complete five-site install now requires the exact original Tick identity and a translated hook-dispatch registration ABI supplied by the modified client.

The registration carries typed callbacks for Tick, ReadState, ReadInventory, ReadTouch, and SubmitActions. The integration attaches the dispatcher before physical hook installation, records the generated action frame, rejects malformed state events, rejects re-entrant Tick dispatch, and uses an atomic disabled-bit/in-flight counter to make detach safe against concurrent hook callbacks.

The implementation still does **not** invent Mojang's internal 1.26.50 ARM64 signatures or offsets. The modified client must translate its exact native ABI into the controller-owned registration ABI.

### v0.24 verification
- GCC Release strict suite: 21/21 passed.
- Clang Release strict suite: 21/21 passed.
- ASan/UBSan focused integration test: passed.
- ThreadSanitizer focused integration test: passed.
- Concurrent Tick/detach regression: passed.
- No warning suppression added.

See `docs/HOOK_DISPATCH_V023.md`.


## v0.25 — coherent native-frame execution

v0.25 makes the five translated native hook events transactional instead of independent pointer notifications. Every event now carries ABI version, protocol 2193, and exact tick metadata. `ReadState` establishes the tick generation; inventory and touch are copied only into that exact generation; `Tick` consumes exactly one complete frame. Stale ticks, backwards ticks, duplicate events, incomplete generations, non-finite numeric state, oversized collections, duplicate entity IDs, duplicate/colliding block observations, and concurrent frame mutation are rejected fail-closed.

In complete five-site mode the dispatcher now requires this coherent native-frame path. The historical typed `ClientAbiTickRunner::run()` path remains available for direct ABI tests and non-five-site callers.

### v0.25 verification
- GCC Release strict suite: 23/23 tests passed.
- ASan/UBSan focused v0.25 test: passed.
- ThreadSanitizer focused v0.25 test: passed.
- Concurrent frame capture regression: passed.
- Dispatch registration ABI v2 metadata-matching regression: passed.
- No warning suppression added.


## v0.26 — Xbox/SISU auth repair and prebuilt Login cache

v0.26 adds a Termux-compatible authentication utility and the matching C++ cache provider for the existing secure Bedrock runtime.

### Xbox authentication change

The legacy helper used the classic `user.authenticate` → `xsts.authorize` chain after Microsoft device-code login. The replacement keeps Microsoft device-code sign-in, then performs signed Xbox device authentication, signed SISU authorization for the Bedrock Android title, and a separate XSTS request explicitly scoped to `https://multiplayer.minecraft.net/`. It then requests the Minecraft Bedrock authentication chain and constructs the protocol-2193 Login payload using a P-384 identity key.

References used for the wire contract:
- [gophertunnel Xbox auth (Bedrock Android configuration and SISU)](https://github.com/Sandertv/gophertunnel/blob/master/minecraft/auth/xbox.go)
- [go-xsapi signed Xbox request contract](https://github.com/df-mc/go-xsapi/blob/main/xal/internal/request.go)
- [gophertunnel Minecraft auth-chain request](https://github.com/Sandertv/gophertunnel/blob/master/minecraft/auth/minecraft.go)

### Termux usage

Install the dependencies once:

```sh
pkg update
pkg install python clang cmake make openssl
python -m pip install --upgrade pip
python -m pip install requests cryptography PyJWT
```

From the extracted project directory, run:

```sh
python tools/xbox_login.py play.example.net 19132
```

Replace `play.example.net` with the exact target host/IP and port. The script prints a Microsoft device code; open the displayed URL, sign in with the Microsoft account that owns Minecraft, enter the code, and return to Termux. It does not ask for or print your Microsoft password. A successful run creates a private `~/.bedrock_ai` cache containing `login_payload.bin`, `ecdsa_p384.pem`, `profile.json`, `chain.json`, and local Xbox device/refresh-token files. **Do not share this directory or its contents.**

The script accepts a custom private cache location through `BEDROCK_AI_CACHE`, for example:

```sh
export BEDROCK_AI_CACHE="$HOME/.bedrock_ai"
python tools/xbox_login.py play.example.net 19132
```

For an IPv6 literal, pass the raw address to the script; it records the cache binding in bracketed `[address]:port` form.

### C++ cache boundary

`FileLoginCacheProvider` loads and validates that cache for the exact target host:port and Bedrock 1.26.50 / protocol 2193. It validates the Login packet envelope, bounds file sizes, parses the P-384 PKCS#8 key, and signs/verifies a probe to check that the cached private/public key pair matches. `Bedrock2193SecureRuntime` now detects the prebuilt-provider interface and sends the cached Login payload while retaining the matching key for the server encryption handshake.

At v0.26 the Android `NativeControllerHandle::connect_raknet` path had not yet been connected to the secure-login runtime. That gap is addressed in v0.27 below. Android still cannot normally read Termux's private `$HOME/.bedrock_ai` directly, so the host must import the restricted cache archive into app-private storage. Do not copy private keys into a permanently world-readable location.

### v0.26 verification
- GCC Release strict build: passed.
- CTest suite: 25/25 passed.
- Python auth protocol tests: 7/7 passed with mocked HTTP responses; they do not contact Microsoft.
- C++ cache test: valid cache accepted; wrong server, protocol mismatch, malformed key, and accidental credential-rebuild path rejected.
- Live Microsoft/Xbox authentication and live Bedrock server login: not tested in this environment.
- Exact retail `libminecraftpe.so` and native hook ABIs are still needed for the embedded-client hook path.


## v0.27 — Android secure-login path, RakNet cookie handling, and timeout diagnostics

v0.27 connects the authenticated cache path to the Android native controller:

- `BedrockAiSecureLogin.importCacheArchive()` imports only `login_payload.bin`, `ecdsa_p384.pem`, and `profile.json` into app-private storage. A companion Termux tool creates this restricted archive without exporting refresh tokens, Xbox device tokens, or other auth cache files.
- `BedrockAiNative.nativeConnectSecureRakNet()` routes to `NativeControllerHandle::connect_secure_raknet()`, which validates the cache against the exact host:port and protocol before opening UDP, then uses `Bedrock2193SecureRuntime` for NetworkSettings, Login, and Bedrock encryption negotiation.
- Android polling now retains the specific lower-layer failure even if the JNI caller omits an error buffer. Timeouts name the stage—waiting for OpenConnectionReply1, OpenConnectionReply2, or ConnectionRequestAccepted—instead of becoming `RakNet poll failed`.
- RakNet timeout and retry timing use a monotonic wall clock. Retries reduce the Request1 MTU probe and retry Request2 using the same cookie challenge values when required.
- The RakNet Reply1 security-cookie challenge is supported. This does **not** mean enabling the legacy RakNet/libcat encrypted-transport mode: vanilla-compatible Bedrock clients send the client-support flag as false and use Bedrock's separate encrypted packet handshake after Login.

### Export the Termux cache for app-private import

Run the login helper first against the exact host and port used by the Android connection. Then create an import archive containing only the three required files:

```sh
python tools/export_android_cache.py "$HOME/.bedrock_ai_android_import.zip"
```

The output archive is credential material: transfer it only to the intended app, import it via Android's Storage Access Framework/document picker using `BedrockAiSecureLogin.importCacheArchive(context, uri)`, and delete transfer copies when no longer needed. The native loader rejects a cache whose recorded server address or protocol does not match the connection.

The included Kotlin/C++ files are integration components, not a complete signed APK or an application UI. The host app must own the document-picker interaction, create/retain `AndroidJcaCrypto` and its native provider handle, configure the native controller, call the secure connect method, and poll it on a background thread.

### RakNet security and timeout behavior

See [`docs/RAKNET_SECURITY_TIMEOUT_V027.md`](docs/RAKNET_SECURITY_TIMEOUT_V027.md). Do not set the vanilla client's RakNet security-support byte to `1` without implementing the legacy Libcat key exchange and packet cipher; doing so is not a safe generic hardening switch.

### v0.27 verification
- Fresh GCC 14 Release build with strict warnings: passed; CTest 27/27 passed.
- Fresh Clang 17 Release build with strict warnings: passed; CTest 27/27 passed.
- AddressSanitizer + UndefinedBehaviorSanitizer on `v27_tests`: passed.
- Python Xbox/auth-cache-export suites: passed (network mocked; no Microsoft traffic).
- Kotlin Android/JCA/cache-import declarations: compiled with `-Werror` against minimal Android API stubs.
- Android JNI/native host translation units: passed strict host-JNI syntax checks. This is not an actual NDK link.
- Live Microsoft/Xbox sign-in and live Bedrock server E2E remain unverified in this environment.
- Android NDK linking and physical-device connection remain pending an Android NDK/device and target server.


## v0.28 — first gameplay-intelligence implementation

v0.28 adds the first reusable tactical-brain module for the Termux controller:

- `GameplayBrain` prioritizes void emergencies, fire/web recovery, food and healing, known mace/wind-charge windows, shield response, weapon choice, ranged attacks, mining, bridging and travel goals. Decisions include a reason, chosen skill, intended hotbar slot, target, movement/aim output and a confidence estimate.
- A 35-entry skill catalogue defines the initial skill domains and their calibration requirements. Definitions without a tested action path are explicitly not considered completed skills.
- `ProjectileSolver` models configured physics and target movement/latency, but refuses uncalibrated profiles or a solution outside the miss tolerance. No Java/default trajectory values are silently reused as Bedrock truth.
- `SkillExperienceBook` persists bounded success/reward summaries in an owner-only TSV file and uses them to adjust future choices. Automatic outcome capture is a remaining integration step; the module cannot learn from an action whose outcome was never observed or recorded.
- The vanilla Bedrock combat policy does not assume Java axe shield-disabling mechanics. A custom server may enable the axe counter only with an explicit capability observation.

See [`docs/GAMEPLAY_BRAIN_V028.md`](docs/GAMEPLAY_BRAIN_V028.md) for the observation contract, research references, safety gates and remaining work.

### v0.28 verification
- GCC 14 Release strict build with all warnings as errors: passed.
- Full CTest suite: 28/28 passed, including 11 new gameplay-brain tests.
- Projectile solver tests cover uncalibrated rejection, trajectory tolerance and moving-target lead.
- Experience-memory tests cover learning-driven recovery selection, persistence and owner-only file permissions.
- Live gameplay is not yet connected end-to-end: raw play-packet decoding, complete inventory/equipment observations, full item-use/block-action codecs and decision-to-network execution remain next-stage work.


## v0.29 — adaptive server rules, cross-play profiles, and dynamic item knowledge

v0.29 introduces per-server/per-mode rule learning and item capability profiles, intended for mixed gameplay across native Bedrock, Java servers accessed through Bedrock proxies such as Geyser, Java-style/custom combat, Hive minigames, and friends' SMP worlds.

- `ServerRulesLearner` learns a conservative combat cadence from controlled, verified observations. It does not infer spam combat or Java cooldown mechanics simply from the client edition, server name, “cracked” login, or the presence of a fake cooldown indicator.
- Mechanic flags are tri-state in practice: a feature is unknown until verified, and explicit verified “unsupported” is stored separately from unknown. Profiles may independently record axe/shield interaction, fireball use, wind-charge recovery, scaffolding, offhand use, and custom item actions.
- `GameplayControlLoop` can persist skill experience, item profiles and learned mechanics under separate directories per host-provided server+mode key, such as `hive:bedwars`, `hive:the_bridge`, or `friends-smp:survival`. Hosts should derive the key from a canonical endpoint/world and current mode, not just a display name.
- `ItemKnowledgeBase` recognizes many common vanilla items and represents custom items by namespace/ID plus exact variant/component key. Items can have multiple separate actions (for example melee vs throw, or place vs use). Calibrated projectile physics is stored against the exact profile variant.
- A custom item's display name is only a discovery hint. It does not authorize use. The profile must specify the observed effect, item capabilities, executable activation(s), and any required calibration. Failed activation can revoke automatic reuse until the profile is re-verified.
- `GameplayControlLoop` supplies the observation → planner → action-sink orchestration seam. A real session host still must provide the complete protocol-2193 decoder and action encoder; this version does **not** claim that all inventory/entity/effect packets or every Hive item action are already wired to the live session.

See [`docs/ADAPTIVE_RULESETS_AND_ITEMS_V029.md`](docs/ADAPTIVE_RULESETS_AND_ITEMS_V029.md) for the research-based rules, server-profile design, item-learning protocol and live-integration boundary.

### v0.29 verification
- GCC 14 Release strict build: passed; full CTest suite 29/29.
- Adaptive rules, persisted positive/negative feature flags, cross-server isolation, exact item variants, multi-action profiles, calibrated custom projectiles and learning-loop integration have focused tests.
- Xbox login protocol and Android cache-export tests remain green; their HTTP interactions are mocked and do not sign in to Microsoft.
- Live Hive/server gameplay and real skill outcomes have not been tested. Exact 1.26.50 inventory/equipment/effect/entity decoding and the complete item-use/block-action encoders remain the integration boundary.


## v0.30 — version-pinned gameplay observation decoder

v0.30 adds `Bedrock2193ObservationDecoder`, a defensive clientbound decoder for an initial set of Minecraft Bedrock 1.26.50 / protocol 2193 packets: StartGame, AddPlayer/AddActor/RemoveActor, absolute/delta entity movement, MovePlayer, SetActorMotion, SetHealth, MobEffect, game-mode updates, motion-prediction hints and authoritative movement corrections. `NativeControllerHandle::read_gameplay_observation()` exposes a fresh snapshot to the C++ host layer after StartGame has been observed in the secure runtime.

Key safeguards:

- Local observation sequence and actual server/player-input tick are separate fields; combat cooldown logic no longer uses packet/frame count as game ticks.
- Head-only rotations don't overwrite player position. Server corrections update local position/velocity and retain authoritative correction type/tick.
- Malformed, unsupported and partially decoded packets are distinct outcomes. Packet counts and observed collections are bounded; entity type strings are preserved rather than converted into invented numeric IDs.
- Other players are not automatically assumed hostile, and client edition/login method are not used as proof of server combat mechanics.
- StartGame/AddPlayer/AddActor handlers explicitly mark partial prefix parsing. Inventory stacks, armor/equipment, attributes, item variant/components, blocks/chunks and outgoing action payloads are still unsupported; no guessed layouts have been added.

See [`docs/PROTOCOL_OBSERVATION_V030.md`](docs/PROTOCOL_OBSERVATION_V030.md) for the original v0.30 movement/state decoder and [`docs/PROTOCOL_OBSERVATION_V031.md`](docs/PROTOCOL_OBSERVATION_V031.md) for the current supported packet list and exact limitations.

### v0.30 verification
- GCC Release strict build: passed; CTest 30/30.
- Includes v0.29 adaptive-rules/item-learning tests and new v0.30 packet/replay tests.
- The new native read API is a decoder/state bridge only. The full inventory/equipment/block decoder and decision-to-outbound-action executor are not yet end-to-end connected.
- No live Hive/SMP session or physical Android/Termux server gameplay was executed in this environment.


## v0.31 — bounded inventory and equipment observation

v0.31 adds protocol-2193 handlers for `InventoryContent` (49), `InventorySlot` (50), `MobEquipment` (31), and `MobArmorEquipment` (32). Inventory snapshots keep container identity separate from slot indices and retain raw `NetworkItemStackDescriptor` fields, including numeric item ID, count, aux value, optional signed stack ID, block runtime ID and opaque binary user data. Single-slot and equipment updates are parsed completely before state changes; offhand equipment cannot overwrite the selected hotbar descriptor; malformed fields, impossible counts, invalid optional flags, trailing bytes and excessive container/slot growth fail closed.

Raw item descriptors are not a semantic item catalogue. Until ItemRegistry and item user-data/component parsing are implemented and independently validated, custom Hive items remain unknown and do not automatically gain capabilities or executable actions. Attributes, entity metadata, chunks/block state, outgoing item-use/item-stack-request/block-action encoding and the end-to-end live decision loop remain incomplete.

### v0.31 verification
- GCC strict Release build: passed; CTest 31/31.
- New `v31_tests` cover inventory snapshots, slot updates, container isolation, held equipment/armor slots, binary user data, unknown-item semantics and malformed/bounded inputs.
- Clang strict Release core + `v31_tests`: passed; the complete Clang all-target build exceeded a 240-second command window while compiling older `v28_tests`/`v29_tests` translation units. ASan/UBSan `v31_tests` with leak detection: passed.
- Live Hive/SMP session and physical Android/Termux behavior remain unverified.


## v0.32 — ItemRegistry identity and actor attributes

v0.32 extended the protocol-2193 observation decoder with `ItemRegistry` (162) and `UpdateAttributes` (29), building on the v0.30 entity/player decoder and v0.31 inventory/equipment handlers.

- `ItemRegistry` entries are length/count/depth bounded. Numeric item IDs are resolved only when the packet gives an unambiguous mapping. Conflicting duplicate IDs are retained as ambiguous, and any old resolved identity is cleared instead of being reused.
- Identifiers supplied by the server are attached to matching inventory, armor, held-item and stored-container descriptors, including registry data that arrives after the inventory packet.
- Each registry entry’s `ItemComponentData` field is parsed as a Network-Little-Endian NBT compound; legacy/non-component entries commonly use an empty compound. Raw NBT is preserved without interpretation, with per-entry/aggregate byte, nesting and collection limits. The parser does not infer custom item powers from that data.
- A small explicit `minecraft:` identifier allowlist classifies familiar item kinds. Custom namespaces such as `hive:` remain `Unknown` without a verified item-capability profile.
- `UpdateAttributes` stores bounds, default/current values and modifier records for tracked actors, then synchronizes observed health/max health, absorption, hunger and armor fields where the attribute name is recognized. Malformed payloads are rejected before any actor/vital/tick state is mutated.
- Registry and attribute packet tests include strict-prefix truncation mutations and state-preservation checks. No protocol information from an unknown or conflicting mapping is guessed.

### v0.32 verification
- GCC strict Release all-target build: passed.
- Full CTest suite: **32/32 passed**.
- Clang strict Release core plus `v30_tests`, `v31_tests` and `v32_tests`: passed. ASan+UBSan `v32_tests` with leak detection: passed. No live Minecraft server or physical Android/Termux test was run for this milestone.

### Still incomplete

This release is not an end-to-end autonomous gameplay bot. Full item/component semantics, entity metadata, terrain/chunk/block collision observations, packet encoders for `PlayerAuthInput` optional interactions/item use, item-stack requests and block actions, and the server-confirmed decision/action/outcome loop remain incomplete. A resolved namespace identifier is not itself proof that the item is safe or supported to use.

See [`docs/PROTOCOL_OBSERVATION_V032.md`](docs/PROTOCOL_OBSERVATION_V032.md) for the exact v0.32 packet coverage and boundaries.


## v0.33 movement action slice

Adds `wire::MovementActionSender`, a movement/rotation-only serverbound `PlayerAuthInput` path through the existing batch/network engine. It requires an in-game session, validates bounded finite movement fields, rejects optional item/block/vehicle payloads, enforces increasing ticks, sends at most one action while awaiting authoritative server-tick feedback, and avoids blind automatic retries after ambiguous send failures. This is not a full autonomous controller and does not implement attack, item-use, inventory-request, or block actions. See `docs/IMPLEMENTATION_STATUS.md` and `docs/NEXT_STAGE.md`.


## v0.34 — local-player movement feedback bridge

v0.34 adds `wire::MovementFeedbackBridge` and an explicit optional `local_player_input_tick` on each observation decode report. The report field is populated only by successfully decoded, non-zero `PlayerInputTick` values that the decoder can tie to the local player: local `MovePlayer`, player-type `CorrectPlayerMovePrediction`, and supported local-player attribute/effect/game-mode updates. Ticks carried by packets for other actors are not valid action acknowledgements.

The bridge can process a clientbound frame through `Bedrock2193ObservationDecoder` and pass only that explicit evidence to `MovementActionSender::observe_server_tick`. It does not acknowledge from a general `WorldSnapshot.server_tick`, local packet counters, tick zero, malformed input, or a vehicle-only correction. This preserves the one-pending-action rule and avoids treating an unrelated high tick as movement feedback. See [`docs/PROTOCOL_FEEDBACK_V034.md`](docs/PROTOCOL_FEEDBACK_V034.md).

### v0.34 verification
- GCC 14 strict Release all-target build: passed.
- Full CTest suite: **34/34 passed**, including the new feedback-bridge suite.
- ASan/UBSan, Android/Termux hardware testing, and live server validation have not been performed for v0.34; no live gameplay success is claimed.
