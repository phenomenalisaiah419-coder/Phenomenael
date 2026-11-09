# Bedrock AI Controller v0.35: audit, changes and honest status

Context date 2026-11-07. Baseline audited: `v0.34` (= v0.33 + movement feedback gate). Target: Bedrock 1.26.50 / protocol 2193.

## 1. Bottom line

- v0.34 was a strong **library** (RakNet, crypto, login, observation slices, policy controllers, 32 passing test suites) but it did not
  yet implement three things the owner asked for: a persistent "second input takes control" rule, survival that **looks ahead** at where the
  human is heading, and learning that rotates through a collection of skills instead of locking onto one.
- v0.35 adds those, plus environment understanding, enemy prediction, custom-item learning, a fair-play gate, a 166-entry honest skill
  catalogue and a closed-loop simulator that tests all of it end to end.
- **It is still not a flagship in the sense of "works inside real Android Minecraft".** Nothing here has touched a live server or a phone
  (this build environment has neither). The honest stage is **~3 of 5**: the shared-control brain is real and tested; the live bridge
  (observation to decision to verified action on a real client) is the remaining work. No claim of "better than Dream/Technoblade/Dakavis" can be
  made or measured until live calibration and thousands of live fights exist.

## 2. What was verified, and how

| Check | Result |
|---|---|
| v0.34 clean build, GCC 13.3 x86_64, `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`, real OpenSSL crypto path | builds, **32/32 C++ suites pass** |
| Python tool tests (`xbox_login`, `export_android_cache`) | 2/2 pass |
| v0.35 (this package): same flags | **36/36 C++ suites pass, 2/2 Python, 0 warnings** |
| Not verified here | ARM64/NDK build, Termux, Clang, sanitizers, any live server, any phone |

Reproduce: `make -j2 check OPENSSL_INC=<dir> OPENSSL_LIB=<libcrypto>` (Makefile added so CMake is not required; CMake lists were updated too).

## 3. Findings in the v0.34 code

| # | Finding | Severity | Status |
|---|---|---|---|
| F1 | `arbitrate()` has no persistent authority: a live human touch always takes a channel, even in Takeover; no contest/hand-back logic | High (core requirement) | Fixed: `TakeoverAuthority` |
| F2 | `SurvivalController` reacts only at `y <= -50` or high fall speed, hard-coded void height, always targets the block under the feet without checking support; cannot handle "human runs toward a void" | High | Fixed: `SurvivalGuardian` + `HazardScanner` |
| F3 | `SkillExperienceBook` is one global tally per skill: no context, no opponent, no exploration, no decay, no cooldown. One good skill is used forever; failures never transfer sensibly | High | Fixed: `SkillLearner` |
| F4 | No notion of unknown vs air for blocks; `block_key()` hash collides for coordinates ~4.19M apart (shown in `test_v36`) | Medium | Fixed in new code: `WorldGrid` (collision-free key, Unknown is first-class). Legacy hash left untouched, flagged |
| F5 | Coordinate convention: `AimController` adds 1.62 eye height to `position`, but Bedrock protocol positions are eye-level and the decoder passes them through | Medium, **unverified** | Open: confirm with one live capture, convert at the decoder boundary. Hub contract states feet positions |
| F6 | `arbitrate()` sets `jump/sneak/sprint` ownership from `mode != Human` even when no AI action asked for them (channel "owned" vs "requested" is muddled) | Low | Documented; hub does not rely on it |
| F7 | Arbiter allows utilities only in emergencies, while the V1 spec lets the AI use pearls/charges autonomously when configured | Low (policy gap) | Kept the stricter default; emergency use goes through the guardian; delegated utilities are a future switch |
| F8 | Learning outcomes were not tied to observed results | Medium | `SkillLearner` is fed only observed health deltas / human vetoes, never "packet sent" |
| F9 | A custom item's behaviour was inferred statically | Medium | Fixed: `ItemLearner` infers from observed effects only |

## 4. What v0.35 adds (all unit- and loop-tested)

| Module | Purpose | Evidence |
|---|---|---|
| `TakeoverAuthority`, `HumanIntentPredictor` | First deliberate counter-input is ignored; a second, separate one inside 40 ticks hands the character to the human; a held input never counts twice; AI re-engages after a hold period (Takeover mode) or by confident predicted intent (Assist mode); emergency overlays everything except the explicit override button | `test_v35`, `test_v37` |
| `WorldGrid`, `MotionPredictor`, `HazardScanner` | Known/unknown blocks, path extrapolation from current inputs, void/lava/lethal-fall/unobserved-drop detection | `test_v36` |
| `SurvivalGuardian` | Overrules the human only when needed: halt+sneak, step back, sneak-bridge forward, block clutch, bucket clutch, pearl escape (refuses without calibrated physics or enough health); validates reach, clickable face, line of sight, never clicks or replaces a bed | `test_v37` |
| `SkillLearner` | Beta-posterior Thompson sampling at 5 levels; failure vs A is weak evidence vs similar players and nearly none vs different ones; cooldown, failure-streak ban, repetition penalty; no exploration in survival-critical moments; atomic persistence | `test_v35`, `test_v38` |
| `OpponentModel` | Rusher/strafer/defensive/evasive/ranged; learned strafe rhythm predicts movement ~45% better than linear extrapolation in the test | `test_v36` |
| `ItemLearner` | Capabilities learned from observed use (human or gated AI trials); harmful consumables flagged on first bad trial; projectile physics fitted and calibrated only after consistent fits; promotes into the existing `ItemKnowledgeBase` | `test_v36` |
| `AutomationPolicy`, `EditionProfile` | Fair-play gate: unknown/public servers default to observe-only; own world and private realms default to full takeover; grants need explicit user confirmation. Bedrock/Java/offline profiles | `test_v37`, `test_v38` |
| `TechniqueRegistry` | 166 techniques with strict maturity labels; 11 blocked by policy | `test_v38`, `docs/SKILL_CATALOG.md` |
| `ControlHub`, `SimWorld` | One tick in, one command out; closed-loop simulator with independent action rules | `test_v37`, `test_v38` |

Bugs the new tests found in the new code (fixed): hop-chain take-off was only checked on the ground; the AI camera turn rotated the human's stick;
mid-air rescues kept the human's drift; aim-then-place cost a tick; this tick's camera flick was ignored when predicting; sprint was assumed for a pure
strafe; learner priors were swamped by heuristics.

## 5. Safety behaviour that is guaranteed by tests

No bed is ever clicked, replaced or broken; nothing is attacked without a measured attack reach **and** the human's attack input;
normal placement needs the human's place input (emergency exception only); unknown terrain is never treated as air or as safe; explicit override
beats everything; Off means off; the pearl is never thrown on guessed physics; the AI never penalises a technique for "no contact" when the
human never allowed an attack.

## 6. What the simulator is not

`SimWorld` reuses the same kinematic model as the predictor, so it checks **control logic, timing budgets and policy gates**, not Minecraft's true physics.
It has no momentum or limited air control; mid-air steering is therefore scored low and used last. Reach, jump, fall, knockback and pearl numbers must come from
the mechanics probe on a controlled server.

## 7. Residual risk and limits (read before trusting it)

- **Rare deaths remain**: in 2,500 deliberately hostile random scripts (hop-spam, random camera flicks, empty kit, lava) the guardian lost 6 characters (0.24%);
  `test_v38` enforces at most 2 of 200. Real-game momentum, latency and knockback will add failure modes.
- **Everything live is unproven**: authentication E2E, transport reachability, movement/attack/place/pearl/bucket action codecs on a real server, same-character embedding.
- **Executable means simulator-verified only**: 20 of 166 catalogue entries; 46 scripted entries need probe data; 89 are catalogued only.
- **Java and cracked**: decision logic, learning and catalogue are edition-aware; there is **no Java wire protocol** and no offline-login path yet.
- **Item learner state is not persisted yet** (skill learner is).
- **The host must feed the hub**: block grid from chunk packets, item counts, enemy list and health. The decoder does not decode chunks yet.
- **Fair play**: automating combat on public competitive servers usually breaks their rules and can get an account banned. The controller ships observe-only there. There is no anti-cheat bypass and there will not be.

## 8. Honest answer to "all skills, all glitches, better than the best players"

The catalogue covers the techniques a human can perform with normal inputs. Exploits that need modified clients or packets (reach, hitbox, velocity, timers, dupes, noclip,
anti-cheat bypass, identity spoofing) are **blocked by design**, not "future work". Beating top players is a measurable goal that needs live calibration, thousands
of recorded fights and a benchmark protocol; none of that can be claimed from simulator results.
