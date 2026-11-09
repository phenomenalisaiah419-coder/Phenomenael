# Implementation Status — Bedrock AI Controller 1.26.50 / 2193

| Area | Status |
|---|---|
| Shared-control AI / human override | Implemented + tests |
| Bed-destruction prohibition | Hard-coded absent from AI action set |
| Movement / aim / combat / bridge / utility / survival | Implemented + tests (controller-level; live observation/action bridge incomplete) |
| GameplayBrain tactical selector + 35-skill catalogue | Implemented + v0.28 tests; observation fields are host supplied |
| Generic calibrated projectile solver | Implemented + tests; Bedrock item profiles uncalibrated by default |
| Skill outcome memory | Implemented + save/load/tests; live outcome collection not wired |
| Protocol identity 1.26.50/2193 | Locked |
| PlayerAuthInput / attack wire | Implemented + tests |
| Game-packet batching + DEFLATE | Implemented + tests |
| Login/bootstrap state machine | Implemented + negative tests |
| RakNet offline handshake | Implemented + tests; v0.27 wall-clock retries, MTU fallback, Reply1 cookie challenge and stage-specific errors |
| Live UDP DatagramIo | Implemented; Linux localhost exercised |
| Reliability: ACK/NACK | Implemented + tests |
| Reliability: resend/retry | Implemented + tests |
| Reliability: ordered/sequenced delivery | Implemented + tests |
| Reliability: fragmentation/reassembly | Implemented + tests |
| Exact Login connection-request builder | Implemented + tests |
| P-384 / ES384 crypto provider | Implemented with OpenSSL + tests |
| ServerToClientHandshake parser/verification | Implemented + tests |
| P-384 ECDH + SHA-256 key derivation | Implemented + tests |
| Bedrock AES-256-CTR packet cipher | Implemented + tests |
| ClientToServerHandshake encrypted response | Implemented + tests |
| Encrypted/compressed 0xFE batch path | Implemented + tests |
| RakNet → bootstrap → network-engine glue | Implemented as secure runtime + local UDP E2E tests; Android secure connect/cache import wired in v0.27 |
| Microsoft/Xbox credential acquisition | External callback/provider boundary; not hard-coded |
| NetherNet/WebRTC transport | Implemented as Android/WebRTC host data-channel adapter + tests |
| Android ARM64 NDK build | CMake/JNI target + CI workflow; actual NDK link requires toolchain/CI; not locally linked here |
| Real 1.26.50 server E2E | Not yet; local UDP reference harness only |
| Current-process `libminecraftpe.so` runtime preparation | Implemented + tests |
| Exact-build hook-plan parser/validator | Implemented + tests |

v0.14 does not claim retail-server compatibility. The implemented secure path has deterministic protocol and crypto tests, plus the complete local runtime state transition from transport-ready → NetworkSettings → Login → ServerToClientHandshake → encrypted ClientToServerHandshake → StartGame/WorldReady.


## v0.15 additions
- Global repository warning policy now uses `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror` for C++ targets.
- Android network lifecycle/recovery boundary added around native RakNet.
- Android JCA P-384/ES384/ECDH/SHA-256 provider and JNI bridge added.
- Replay backend added for sanitized session ingestion.
- NetherNet/WebRTC reliable-data-channel adapter added using real 0xFE Bedrock batch bytes.
- Live compatibility runner added; real credential material remains host-supplied.
- Embedded in-process `bedrockai_embedded_*` tick/action ABI added.
- v0.15 remains unverified for a real retail-server login and unlinked Android ARM64 binary in this environment.

## v0.15 final verification
- Clean GCC Release build with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`: 13/13 tests passed.
- Clean Clang 17 Release build with the same warning policy: 13/13 tests passed.
- Clean AddressSanitizer + UndefinedBehaviorSanitizer build with leak detection: 13/13 tests passed.
- ThreadSanitizer build of the concurrency-sensitive v0.15 target: PASS.
- Kotlin Android/JCA source set compiled with `-Werror`; P-384 keygen, ES384 sign/verify, ECDH and SHA-256 runtime checks passed.
- JNI and Android-simulated native translation units compiled with strict warnings.
- Historical warning defects exposed by the stricter compiler matrix were fixed; no warning suppression was added to hide them.


## v0.16 additions
- Version-pinned embedded client native host contract for Bedrock 1.26.50 / protocol 2193.
- Real client state/inventory/touch/action callback seams mapped into the existing controller core.
- Native tick callback registration seam for the modified client process.
- Loaded `libminecraftpe.so` image discovery/validation on Android/Linux.
- Stable C ABI + JNI lifecycle surface for embedded integration.
- No unverified game-specific addresses or signatures invented; exact binary hook binding remains dependent on the target 1.26.50 ARM64 library.


## v0.16 additions
- Embedded 1.26.50/2193 native host ABI with required state, inventory, touch, AI-decision and action-submission callbacks.
- Full client entity/block collections mapped into the controller `WorldSnapshot`.
- Real client-tick registration callback.
- Loaded `libminecraftpe.so` discovery/validation on Android/Linux.
- Extended embedded AI command path for attack, place, item and utility proposals while preserving human gates.
- Exact binary hook offsets remain intentionally unclaimed until the authorized target ARM64 library is supplied and signature-verified.


## v0.17 additions
- ELF64/AArch64 loaded client image fingerprinting and executable-segment discovery.
- Wildcard signature parsing/scanning with unique-match enforcement.
- Exact-build, expected-byte, patch-range and overlap validation.
- Verified hook installer boundary integrated with the embedded client runtime.
- Android JNI/Kotlin client-image diagnostic report.
- Exact retail/custom 1.26.50 ARM64 hook signatures and first physical device E2E remain unverified because the target binary/device is not present in this environment.

## v0.17 final verification
- GCC Release with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`: 15/15 tests passed.
- Clang Release with the same warning policy: 15/15 tests passed.
- AddressSanitizer + UndefinedBehaviorSanitizer with leak detection: 15/15 tests passed.
- Kotlin JVM compile with `-Werror` for the new Android device-probe/native declarations: passed.
- JNI native translation-unit syntax check with strict diagnostics: passed.
- No warning suppression was added; historical compatibility warnings exposed by the strict policy were fixed.
- v0.14 source preservation audit: 0 missing files.
- Exact retail/custom 1.26.50 ARM64 hook signatures and physical device E2E remain unverified because the target `libminecraftpe.so` and Android device/emulator are not present in this environment.

## v0.18 historical additions
- Conservative live AArch64 inline Tick hook runtime.
- Single-instruction `B` patch to a nearby executable veneer, avoiding multi-instruction live patch tearing.
- Register-preserving universal ARM64 Tick entry stub covering x0-x30, q0-q7 and NZCV.
- Executable trampoline with BTI and absolute return to the original function at +4.
- Fail-closed rejection of PC-relative, indirect-control-flow and return instructions in the relocated first instruction.
- Original-byte revalidation immediately before patching, executable-range checks, page-protection preflight/restore and instruction-cache flushes.
- JNI API for supplying an exact build ID/signature/expected instruction to the verified installer.
- No production offsets/signatures fabricated; exact client binary remains required for physical hook activation.

### v0.20 process runtime
- Implemented: current-process `libminecraftpe.so` discovery, exact build-ID validation, executable-range validation, exported-symbol guard, multi-hook plan parser/validator, Android JNI process-runtime handle.
- Not implemented: build-specific Mojang offsets/signatures and physical state/input/action hook installation without the exact target ARM64 binary.


## v0.20 additions
- Exact current-process `libminecraftpe.so` discovery and preparation runtime.
- Optional exact GNU build-ID verification before preparation succeeds.
- PID/image report and refresh support with image-change detection.
- Exported-symbol resolver constrained to verified executable segments of the loaded client image.
- Machine-readable seven-field multi-hook plan parser/validator covering Tick, state, inventory, touch and action sites.
- Single-build-ID and mandatory-Tick validation for hook plans.
- Android JNI process-runtime lifecycle and report methods.
- No arbitrary second game library load; `RTLD_NOLOAD` is used where available.

## v0.20 verification
- GCC Release strict suite: 18/18 passed.
- Clang 17 Release strict suite: 18/18 passed.
- ASan + UBSan v0.20 target with leak detection: passed.
- Complete five-site hook-plan binding test: passed.
- JNI strict syntax compile: passed.
- Kotlin declaration compile: passed.
- v0.18 → v0.20 source preservation: 0 missing files.
- No warning suppression added.
- Physical 1.26.50 ARM64 hook activation remains pending the exact target `libminecraftpe.so` and verified signatures.


## v0.20 complete hook-plan binding
The integration validates one exact five-site plan (`Tick`, `ReadState`, `ReadInventory`, `ReadTouch`, `SubmitActions`) against the loaded build ID and executable segments, then passes the complete verified binding set to the modified client’s `install_client_hooks` callback. The callback is where the exact ABI-specific native hooks are installed; this project deliberately does not guess Mojang function signatures or offsets.


## v0.21

Implemented: typed five-site ARM64 client ABI adapter, same-character tick runner, immutable ABI table identity checks, and adapter regression tests. Exact Mojang site addresses/signatures remain supplied by the exact verified 1.26.50 ARM64 client binary.

## v0.22

Added `ClientHookDispatcher`, which is the runtime event boundary behind the exact-ABI client installer. It exposes typed dispatch for Tick, ReadState, ReadInventory, ReadTouch, and SubmitActions and keeps per-site counters plus rejection counts. The dispatcher never infers Mojang signatures; the exact client ABI remains supplied by the modified client.

## v0.23

Implemented the five-site dispatcher as a registered translated hook ABI rather than a standalone helper. The verified client hook plan now has a fail-closed registration boundary carrying typed callbacks for Tick, ReadState, ReadInventory, ReadTouch, and SubmitActions.

The integration now:
- requires the exact original Tick identity before installing the complete five-site plan;
- attaches a typed `ClientAbiTable` to the dispatcher using the already supplied host callbacks;
- registers all five translated hook callbacks before physical hook installation;
- rejects a complete hook plan when the translated dispatch ABI is unavailable;
- tracks the latest action frame produced by a real dispatcher Tick event;
- rejects malformed state events before they enter controller logic;
- uses an atomic disabled-bit/in-flight counter so detach cannot race a callback into a destroyed dispatcher;
- rejects re-entrant Tick dispatches;
- preserves the existing five-site build-ID/signature/range/overlap validation.

This remains deliberately one layer short of Mojang's unknown internal ABI: the modified 1.26.50 client must translate its exact native hook arguments into the typed registration ABI. No retail offsets or function signatures were fabricated.

## v0.23 verification
- GCC Release strict suite: 21/21 tests passed.
- Clang Release strict suite: 21/21 tests passed.
- ASan + UBSan focused v0.23 integration test: passed.
- ThreadSanitizer focused v0.23 integration test: passed.
- Concurrent Tick/detach regression: passed.
- No warning suppression added.


## v0.24 — Native ABI certificate and verified translator boundary

- Added `ClientNativeAbiCertificate` and `ClientNativeAbiValidator`.
- Complete five-site installation now requires a certificate reader from the modified client.
- Certificate is bound to protocol 2193, 64-bit pointers, exact loaded GNU build ID, exact verified hook target addresses, and five distinct translator addresses inside executable `libminecraftpe.so` ranges.
- Expanded `EmbeddedClientHostApi` with the certificate reader and bumped the host ABI from version 1 to version 2 to prevent old hosts from being misread.
- Certificate validation occurs before translated dispatch registration and before physical five-site installation.
- Existing same-character control path, human attack/place gates, AI bed-destruction prohibition, survival exceptions, and fail-closed hook validation remain unchanged.
- v0.24 tests cover valid certificates, build-ID mismatch, hook-target mismatch, missing translator, duplicate translator, pointer-width mismatch, and complete-plan integration.


## v0.25 — Coherent native-frame execution

- Added `ClientNativeFrameAssembler` for tick-coherent state/inventory/touch capture.
- Complete five-site dispatch now uses consumed native frames rather than independently re-reading client state.
- Added stale/backwards/duplicate/incomplete generation checks.
- Added finite-value, collection-bound, duplicate-entity, and block-key collision guards at the native-to-core conversion boundary.
- Added non-blocking atomic frame serialization and atomic readiness state.
- Added coherent-dispatch, duplicate-data, and concurrent-capture regressions.

- Bumped translated five-site dispatch registration ABI from v1 to v2 and attached exact tick metadata to every native hook event.
- Added exact metadata-tick matching for state, inventory, touch, and Tick consumption.


## v0.26 — Xbox/SISU authentication and prebuilt Login cache

- Reworked `tools/xbox_login.py` to keep the Microsoft device-code flow, use signed Xbox device authentication + SISU, and request XSTS for `https://multiplayer.minecraft.net/` instead of relying on the legacy user.authenticate → XSTS sequence.
- Added explicit stage/XErr diagnostics that do not log bearer tokens or refresh tokens.
- Persists a P-256 Xbox device identity and optional refresh token in the private Termux cache; generates a fresh P-384 Bedrock identity key and the protocol 2193 Login payload.
- Added `FileLoginCacheProvider`, which binds the cache to the exact server host:port, validates protocol and payload envelope, caps file sizes, loads the PKCS#8 P-384 identity key, and verifies the public/private key match.
- `Bedrock2193SecureRuntime` now uses `BedrockPrebuiltLoginProvider` when supplied and preserves the cached P-384 identity key through the encryption handshake.
- GCC strict Release build passed; CTest 25/25 passed; Python auth tests 7/7 passed with mocked HTTP.
- Real Microsoft/Xbox token exchange and live Bedrock login were not possible in the build environment.
- Important remaining integration: Android's existing `NativeControllerHandle::connect_raknet` still attaches `PortableControllerRuntime` rather than `Bedrock2193SecureRuntime`; the Android app also needs a secure cache-import mechanism from Termux/document picker into app-private storage.
- The exact retail Bedrock 1.26.50 `libminecraftpe.so` is still required for native hook ABI work.


## v0.27 — authenticated Android connection + RakNet diagnostics

- Added `NativeControllerHandle::connect_secure_raknet()` and JNI/Kotlin bridge to use the target-bound prebuilt Login cache with `Bedrock2193SecureRuntime`.
- Added app-private Android cache-archive import and a Termux export helper that includes only the Login payload, P-384 identity key, and profile metadata; refresh and Xbox-device tokens are excluded from the transfer archive.
- Android cache import preserves the owner execute/search permission on directories while keeping secret files non-executable and owner-only.
- Added RakNet Reply1 security-cookie parsing/echo; a false client legacy-security-support byte is retained for vanilla Bedrock compatibility.
- Timeout decisions use monotonic wall-clock deadlines, timed retransmissions and MTU fallback, with a stage-specific error preserved all the way through `nativeLastError()`.
- Live sign-in, a real Bedrock server session, Android NDK link, and physical-device runtime remain unverified in this environment.


### v0.27 verification evidence

- GCC 14 Release strict build: passed; CTest 27/27 passed.
- Clang 17 Release strict build: passed; CTest 27/27 passed.
- ASan + UBSan `v27_tests` with leak detection: passed.
- Termux auth helper and Android cache exporter tests: passed without external network calls.
- Kotlin cache-import/JCA API source compiled with strict warnings using minimal Android API stubs.
- Android JNI/native translation units passed strict host-JNI syntax checks; this does not substitute for an Android NDK link.
- Live Microsoft login, live Bedrock server E2E, and physical Android runtime remain unverified.


## v0.28 Gameplay intelligence status

- Implemented: decision engine for the high-priority survival, healing, food, combat style, shield, item-selection, calibrated ranged aim, mining, bridge and waypoint cases listed in `docs/GAMEPLAY_BRAIN_V028.md`.
- Implemented: 35-entry prebuilt skill catalogue, generic configurable trajectory solver, and outcome-statistics save/load.
- Verified: CTest 28/28 passed in the fresh GCC Release build, with strict compiler warnings.
- Not yet implemented: live 1.26.50 play-packet parser feeding all `GameplayObservation` fields; full inventory/equipment/effects/entity/block observation pipeline; item-use/stack-request/block-action encoders; action executor tying each decision into the encrypted runtime; automatic outcome recording.
- The current brain is a deterministic rules/skill foundation with adaptive outcome scoring, not a pretrained general-purpose neural model. It does not require the user to teach individual moves to populate the initial skill definitions, but it needs server-confirmed executions before it can learn which techniques work reliably in that environment.


## v0.30 — inbound gameplay observation

- Implemented: a strict, protocol-2193 `Bedrock2193ObservationDecoder` for the packet prefixes and full packet shapes documented in `PROTOCOL_OBSERVATION_V030.md`.
- Implemented: native C++ snapshot read bridge requiring secure runtime InGame plus an observed StartGame packet.
- Implemented: separate local frame counter vs version-defined server/input tick; cooldown gate uses real tick only when known.
- Implemented: bounded counters, malformed/truncated handling, finite-value checks, server correction updates, effect lifecycle and opt-in targeting of player entities.
- Verified: GCC strict Release build and 30/30 CTest suite passed.
- Still missing: live InventoryContent/InventorySlot item-descriptor decoder, MobEquipment/Armor, UpdateAttributes, metadata, chunk/block map, outgoing item-use/stack-request/block actions, and live decision/action/confirmation loop.
- Live server and physical-device tests remain unverified.


## v0.31 — inventory and equipment wire decoding

- Implemented bounded protocol-2193 `InventoryContent` (49), `InventorySlot` (50), `MobEquipment` (31), and `MobArmorEquipment` (32) handlers.
- Implemented raw `NetworkItemStackDescriptor` retention: item ID, amount, aux value, optional signed net ID, block runtime ID, opaque length-prefixed binary user data, and container identity metadata.
- Inventory slot updates validate their complete packet before replacing state; strategy inventory is refreshed only from known canonical player inventory/hotbar containers, not arbitrary chest/entity containers. Offhand equipment cannot overwrite the held main-hotbar observation.
- Added focused `v31_tests`; GCC strict Release build and CTest 31/31 pass. Clang strict Release core + `v31_tests`: passed; full all-target Clang build exceeded the 240-second command window while compiling older test translation units. ASan/UBSan `v31_tests` with leak detection: passed.
- Still missing: ItemRegistry ID/name mapping and opaque user-data/component parsing, `UpdateAttributes` (29), full entity metadata, world chunks/block collision map, outbound item-use/stack-request/block/equipment actions, and the server-confirmed decision/action loop.
- Raw descriptors are not sufficient evidence for item capabilities. Unresolved vanilla/custom item IDs stay `Unknown`; live Hive/SMP and physical Android/Termux tests remain unverified.


## v0.32 — item registry identity and actor attributes

- Implemented bounded protocol-2193 `ItemRegistry` (162) parsing, validated NBT structure preservation for every registry item-data field, collision-safe ID lookup and retroactive registry identity resolution for inventory/equipment/storage observations.
- Implemented protocol-2193 `UpdateAttributes` (29) parsing with finite numeric fields, bounded modifiers, actor attribute state, local health/max-health, absorption, hunger and armor synchronization, and separate server tick observations.
- Custom namespaces remain capability-unknown; the vanilla item kind classification is an explicit limited allowlist, not a complete item catalogue. Registry NBT is preserved but not semantically decoded; non-component entries commonly contain an empty compound.
- Verified: GCC strict Release all-target build; full CTest **32/32 passed**. Focused tests cover collisions, retroactive updates, malformed NBT, excessive counts, and every strict-prefix truncation of registry/attribute specimens without partial state mutation.
- Not yet verified: live Microsoft login through this exact release, live protocol-2193 server session, live Hive/SMP behavior, physical Termux/Android runtime.
- Still missing: complete component semantics/item catalog, entity metadata, chunks/block/collision map, full versioned outgoing action codecs/execution, and authoritative observation → decision → action → confirmation → learning integration.


## v0.33 outbound movement

`wire::MovementActionSender` now connects movement-only `PlayerAuthInput` encoding to the existing network engine. It enforces game-state, tick, numeric-bound, optional-field, batch-boundary, and feedback gates. Its one-pending-action rule is released only by explicit server-tick feedback; the current class is not yet automatically wired to every decoded server observation, so integration is still required. No live server/device gameplay validation has been done.


## v0.34 — movement feedback bridge

- Added `ObservationDecodeReport::local_player_input_tick`, reset for every packet and populated only when a successfully decoded non-zero `PlayerInputTick` is tied to the local player.
- Added `wire::MovementFeedbackBridge::process_frame`, which routes a clientbound frame through the protocol-2193 decoder and calls the movement sender feedback gate only when this explicit local-player evidence exists.
- Unrelated actor ticks, zero ticks, vehicle-only correction packets, and malformed packets cannot release a pending movement action.
- GCC 14 strict Release all-target build and full CTest suite: **34/34 passed**. Live server/device verification and sanitizer runs remain unverified.


## v0.35 (shared-control intelligence)

Added and tested (see `docs/AUDIT_V035.md`): `TakeoverAuthority` (second-input takeover, emergency overlay, explicit override), `HumanIntentPredictor`,
`WorldGrid`/`MotionPredictor`/`HazardScanner`, `SurvivalGuardian`, `SkillLearner`, `OpponentModel`, `ItemLearner`, `AutomationPolicy`/`EditionProfile`,
`TechniqueRegistry` (166 entries), `ControlHub`, `SimWorld`. Suites `test_v35`..`test_v38`.

Status per component: code yes, unit yes, simulator closed-loop yes, **live no**. Java wire protocol: not implemented. Offline/cracked login: not implemented.
Item-learner persistence: not implemented. Chunk decoding: not implemented (host must supply `WorldGrid`). Honest stage: ~3/5.
