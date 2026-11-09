# Adaptive rulesets and dynamic items — v0.29

Target build: Minecraft Bedrock 1.26.50 / protocol 2193.

## Why rules are per server and mode

The controller must not equate the client edition with the combat rules. Bedrock players can join Java servers through a translator such as Geyser, where the server's gameplay rules still matter. However, Java-style cross-play does not always mean modern Java 1.9+ attack cooldowns: a server may run older combat, a combat plugin, minigame-specific damage rules or custom abilities. “Cracked” is an authentication/access policy, not a reliable signal for mechanics.

Geyser's `show-cooldown` configuration is particularly easy to misread: Geyser documents that it sends a *fake cooldown indicator* to Bedrock clients to communicate Java-style cooldown, because the Bedrock client normally uses different combat presentation. That indicator is UI feedback, not proof that a particular attack was accepted at full damage by the current server. Source: <https://github.com/GeyserMC/Geyser/wiki/Understanding-the-Config>.

Therefore the controller uses a stable profile key supplied by the session/observation adapter. Good keys distinguish both endpoint and mode, for example:

- `hive:bedwars`
- `hive:the_bridge`
- `hive:skywars`
- `friends-smp:survival`
- `crossplay-practice:java-cooldown`

These are examples rather than a built-in list. A mode transition should switch keys. Host code should derive them from the canonical server identity and a trusted mode signal where available. If the mode cannot be determined, use an explicit `unknown` mode rather than merging potentially incompatible experience.

The official protocol docs pin Bedrock 1.26.50 to protocol 2193 and show that `PlayerAuthInputPacket` carries movement plus optional item-use, item-stack request and block-action data. The same version changed `ItemUseInventoryTransaction` by adding `HandSlot` and shifting the item/position/cooldown fields. The real packet decoder/encoder must follow this exact schema rather than reusing an older layout. Source: <https://mojang.github.io/bedrock-protocol-docs/1.26.50/packets/player-auth-input-packet/>.

## Learning attack cadence without a fixed fighting style

`ServerRulesLearner` does not preselect “spam” or “cooldown” from a server name. It only accepts an `AttackTrial` when:

- the interval is bounded and valid;
- target vulnerability is known and true;
- damage is normalized to a valid range;
- confounders such as armor, invulnerability frames, resistance, weapon effects, target state and other damage are controlled;
- accepted/rejected status and damage are actually observed.

Repeated short-interval full-damage trials support a spam-friendly classification. Repeated reduced/rejected short-interval trials together with full-damage longer intervals support a cooldown-gated classification. The estimate remains `Unknown` when evidence is insufficient or inconsistent. The classifier must not treat ordinary jitter, armor, a shield, a custom damage multiplier, a missed hit, or stale target health as evidence of cooldowns.

Feature support is explicit and versioned per profile. Each feature bit distinguishes “observed supported”, “observed unsupported” and “not verified yet.” An explicit negative is important: for example, verifying that axe/shield punishment does not work on one Bedrock mode must not inherit a positive result learned on another mode. Any server rule with material PvP impact should be tested in a controlled location and re-verified after a ruleset or game update.

The adaptive combat selector scores aggressive pressure, strafing/circling, defensive reset, chase, finish opportunities and multi-target response using the current distance, health, enemy count, game mode and outcome memory. That makes the *style choice* situation-dependent instead of a single style being used every fight. The experience book stores observed skill results; a successful packet write alone is deliberately not a successful gameplay outcome.

## Item discovery and action profiles

### Item identity must be exact

Known vanilla item IDs give a useful starting catalogue. Custom/Hive items require server-specific evidence because display names and lore can be renamed, translated or reused, and one item may have different variants or abilities. The key includes the exact namespace/ID and a `variant_key` supplied by the host, ideally derived from decoded components/metadata/effect/ability identity.

For example, a trident may have melee and thrown actions, a water bucket may place water or scoop a source depending on state, and a potion container is not automatically considered healing merely because its item ID is `minecraft:potion`. Healing must be supported by its decoded effect/variant. Different variants with the same identifier do not inherit each other's executable profile.

### A profile has capabilities and executable activations

A custom item profile can include one or more verified capabilities (melee, ranged, food, healing, defence, projectile, teleport, knockback, burst movement, block placement, clutch recovery, mining, fire control, web escape, bridge utility, objective damage, drinkable/throwable, mobility, resource utility and other explicit custom use) and several separate activations (attack/use/consume/throw/place/mine/defend/interact). Projectile abilities additionally require calibrated physics for the exact profile.

The name `Rescue Platform` may suggest clutch recovery, but the name alone cannot establish whether it places a block, teleports the player, launches a projectile or has a cooldown. The resolver can create a suggestion for triage but does not authorize using an unknown custom item. A profile becomes executable only after its observed effect and activation are verified. A failed outcome can revoke the profile and require re-verification. This avoids accidentally consuming a rare item or firing a dangerous custom ability because of a guessed label.

This is the design for adapting to Hive and custom SMP inventories: scan what the server actually puts into inventory, resolve known vanilla IDs immediately, match exact previously verified custom variants for that server/mode, and rank currently available actions against the immediate situation. It avoids hard-coding a BedWars shopping order, and it does not assume the bot will buy the same kit every round. Items missing from the current inventory are not considered available; hotbar slot and readiness constraints still apply.

The official Hive pages list multiple active minigames, including BedWars, SkyWars, Survival Games, Ground Wars, Capture the Flag and The Bridge. That reinforces why `hive` alone is not a sufficiently precise learning key: the minigame must be part of the profile. Sources: <https://playhive.com/> and <https://playhive.com/bedwars/>. Public game pages do not provide a complete, stable machine-readable database of every current item, kit, shop item, upgrade or server-only action. This release therefore does not claim to have a pre-verified profile for every Hive-specific item. It provides the exact-variant profile system to learn and store them safely as observable evidence becomes available.

## Outcome-driven learning and persistence

The loop may learn from `verified_attack_trial` and `verified_item_outcome` attached to a fresh observation. Those fields must only be populated by a real adapter after the relevant action's result is observable and material confounders are handled. The loop persists:

- `skills.tsv`: per-skill experience summaries;
- `items.tsv`: exact item/variant capability profiles and outcomes;
- `server-rules.tsv`: controlled combat trials and verified feature flags.

Call `GameplayControlLoop::configure_learning_directory()` before the first tick if persistence is needed; the host should use a private Termux directory such as `$HOME/.bedrock_ai/gameplay_profiles`. Each distinct server+mode key is stored beneath a separate hashed directory, so an item learned on Hive BedWars does not automatically become usable on a friend's SMP. Keep this directory private and back it up only when desired.

The learning loop is not self-magical: it can only learn from observations provided by the host. If inventory packets omit custom components, target health is hidden, a projectile impact cannot be distinguished, or a hit's damage was affected by unknown armor/effects, the host should not manufacture a “verified” result. Unknown mechanics remain unknown until there is good evidence.

## Current live-integration boundary

v0.29 implements the decision/learning side: adaptive rules, dynamic item profiles, exact variant handling, per-mode persistence and a `GameplayControlLoop` that connects an observation source, planner and action sink through explicit interfaces. It does **not** yet complete live protocol 2193 observation and execution end to end.

The next protocol work must decode real entity spawn/movement/despawn, local player state/attributes, inventory contents/slots, armor/equipment, effects, blocks/chunks and session mode; identify the exact item components/variants; and encode legal item-use, attack, inventory, movement and block actions. It must then verify the result from server updates. Until that host adapter is connected, passing the tests validates the brain and the learning boundaries—not live play on Hive or arbitrary SMP worlds.
