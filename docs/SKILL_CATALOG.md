# Skill catalogue (generated from `core/src/technique_registry.cpp`)

Total **166** entries: **20** executable (simulator-verified), **46** scripted (need live calibration), **89** planned, **11** blocked by policy.

Maturity is deliberately strict. *Executable* means the decision logic and an executor exist in this repository and pass the
closed-loop simulator tests; it does **not** mean it has been proven in a real Minecraft client.
*Blocked* entries are things the controller will never do (bed destruction, economy automation, exploits, anti-cheat evasion).

## melee combat

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `combat.strafe_orbit` | Strafe orbit | Bedrock + Java | executable (simulator-verified) | medium | no | enemy in range |
| `combat.pressure_chase` | Pressure chase | Bedrock + Java | executable (simulator-verified) | medium | no | enemy retreating or stationary |
| `combat.kite_back` | Kite back | Bedrock + Java | executable (simulator-verified) | low | no | enemy approaching |
| `combat.retreat_reset` | Retreat and reset | Bedrock + Java | executable (simulator-verified) | low | no | health low or outnumbered |
| `combat.aim_lead` | Predictive aim | Bedrock + Java | scripted (needs live calibration) | low | no | opponent tracked, live hit-cone measured |
| `combat.target_switch` | Target switching | Bedrock + Java | scripted (needs live calibration) | medium | no | two or more enemies |
| `combat.w_tap` | W-tap | Bedrock + Java | scripted (needs live calibration) | low | yes | measured sprint-reset timing |
| `combat.s_tap` | S-tap | Bedrock + Java | scripted (needs live calibration) | low | yes | measured spacing |
| `combat.jump_reset` | Jump reset | Bedrock + Java | scripted (needs live calibration) | low | yes | knockback model measured |
| `combat.crit_jump` | Critical-hit jump | Bedrock + Java | scripted (needs live calibration) | low | yes | falling-attack rule measured |
| `combat.combo_lock` | Combo lock | Bedrock + Java | scripted (needs live calibration) | medium | yes | hit cadence measured |
| `combat.cps_pace` | Measured attack pacing | Bedrock + Java | scripted (needs live calibration) | low | yes | attack cadence from the measured profile |
| `combat.block_hit` | Block-hit | Java | planned | low | yes | Java 1.8-style sword block |
| `combat.hit_select` | Hit selection | Java | planned | low | yes | attack-cooldown model |
| `combat.sweep_edge` | Sweep edge | Java | planned | low | yes | sweep rule measured |
| `combat.shield_bait` | Shield bait | Bedrock + Java | planned | medium | no | enemy with shield |
| `combat.axe_shield_break` | Axe shield break | Bedrock + Java | planned | low | no | axe in hotbar |
| `combat.mace_smash` | Mace smash | Bedrock + Java | planned | high | yes | mace + height |
| `combat.spear_lunge` | Spear lunge | Bedrock + Java | planned | medium | yes | spear item present, profile learned |
| `combat.wall_pin` | Wall pin | Bedrock + Java | planned | medium | no | wall behind enemy |
| `combat.edge_knock` | Edge knock | Bedrock + Java | planned | medium | no | enemy near an edge |
| `combat.jump_dodge` | Jump dodge | Bedrock + Java | planned | low | no | incoming melee |
| `combat.heal_trade` | Heal while trading | Bedrock + Java | scripted (needs live calibration) | medium | no | golden apple/potion |
| `combat.block_cover` | Block cover | Bedrock + Java | scripted (needs live calibration) | medium | no | blocks available |
| `combat.rod_combo` | Rod combo | Bedrock + Java | planned | medium | yes | fishing rod |

## ranged combat

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `ranged.bow_lead_shot` | Bow lead shot | Bedrock + Java | scripted (needs live calibration) | low | yes | calibrated bow physics |
| `ranged.bow_charge` | Bow charge timing | Bedrock + Java | scripted (needs live calibration) | low | yes | measured charge curve |
| `ranged.crossbow_burst` | Crossbow burst | Bedrock + Java | planned | low | yes | crossbow loaded |
| `ranged.trident_throw` | Trident throw | Bedrock + Java | planned | low | yes | trident |
| `ranged.snowball_knock` | Snowball/egg knock | Bedrock + Java | planned | low | yes | snowballs/eggs |
| `ranged.fireball_punch` | Fireball punch | Bedrock + Java | planned | medium | yes | fire charge |
| `ranged.wind_charge_knock` | Wind charge knock | Bedrock + Java | scripted (needs live calibration) | medium | yes | wind charge, calibrated |
| `ranged.potion_throw` | Splash potion throw | Bedrock + Java | planned | low | yes | splash potions |
| `ranged.cobweb_trap` | Cobweb trap | Bedrock + Java | planned | low | no | cobwebs |
| `ranged.lava_trap` | Lava bucket trap | Bedrock + Java | planned | high | no | lava bucket + water to clean up |
| `ranged.tnt_trap` | TNT trap | Bedrock + Java | planned | high | no | TNT + flint |

## movement

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `move.sprint_jump` | Sprint jump | Bedrock + Java | scripted (needs live calibration) | low | yes | jump profile measured |
| `move.strafe_jump` | Strafe jump | Bedrock + Java | scripted (needs live calibration) | low | yes | air-control measured |
| `move.sneak_edge_walk` | Sneak edge walk | Bedrock + Java | scripted (needs live calibration) | low | no | ground edge |
| `move.parkour_gap1` | 1-block gap jump | Bedrock + Java | scripted (needs live calibration) | medium | yes | gap <= 1 |
| `move.parkour_gap2` | 2-block gap jump | Bedrock + Java | scripted (needs live calibration) | medium | yes | gap <= 2 |
| `move.parkour_gap3` | 3-block gap jump | Bedrock + Java | scripted (needs live calibration) | high | yes | gap <= 3 measured |
| `move.parkour_gap4` | 4-block gap jump | Bedrock + Java | planned | high | yes | gap <= 4 measured |
| `move.neo_jump` | Neo jump | Bedrock + Java | planned | high | yes | wall + gap |
| `move.ladder_climb` | Ladder/vine climb | Bedrock + Java | planned | low | no | ladder or vine |
| `move.scaffold_climb` | Scaffolding climb | Bedrock + Java | planned | low | no | scaffolding |
| `move.swim_sprint` | Swim sprint | Bedrock + Java | planned | low | no | water |
| `move.boat_ice` | Boat on ice | Bedrock + Java | planned | low | no | boat + ice |
| `move.elytra_fly` | Elytra flight | Bedrock + Java | planned | high | yes | elytra + fireworks |
| `move.minecart_boost` | Minecart ride | Bedrock + Java | planned | low | no | rails |
| `move.mount_ride` | Mount riding | Bedrock + Java | planned | low | no | horse/saddle |
| `move.slime_bounce` | Slime bounce | Bedrock + Java | planned | low | no | slime block |
| `move.bubble_column` | Bubble column | Bedrock + Java | planned | low | no | bubble column |
| `move.web_escape` | Web escape | Bedrock + Java | planned | low | no | inside cobweb |
| `move.powder_snow_escape` | Powder snow escape | Bedrock + Java | planned | medium | no | powder snow |
| `move.pearl_traverse` | Pearl traverse | Bedrock + Java | scripted (needs live calibration) | medium | yes | calibrated pearl |
| `move.wind_charge_launch` | Wind-charge launch | Bedrock + Java | scripted (needs live calibration) | medium | yes | wind charge, measured impulse |
| `move.fireball_launch` | Fireball launch | Bedrock + Java | planned | high | yes | fire charge |
| `move.knockback_recover` | Knockback recovery | Bedrock + Java | scripted (needs live calibration) | medium | no | knocked airborne |

## bridging

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `bridge.sneak_straight` | Sneak bridge (straight) | Bedrock + Java | executable (simulator-verified) | low | no | blocks + clickable face |
| `bridge.sneak_diagonal` | Sneak bridge (diagonal) | Bedrock + Java | executable (simulator-verified) | low | no | blocks + clickable face |
| `movement.pillar_up` | Pillar up | Bedrock + Java | scripted (needs live calibration) | low | no | blocks, open headroom |
| `bridge.staircase_up` | Staircase bridge up | Bedrock + Java | scripted (needs live calibration) | low | yes | blocks |
| `bridge.staircase_down` | Staircase bridge down | Bedrock + Java | scripted (needs live calibration) | low | yes | blocks |
| `bridge.ninja` | Ninja bridge | Bedrock + Java | scripted (needs live calibration) | medium | yes | backward sneak timing measured |
| `bridge.god` | God bridge | Bedrock + Java | scripted (needs live calibration) | high | yes | placement cadence measured |
| `bridge.moonwalk` | Moonwalk bridge | Bedrock + Java | scripted (needs live calibration) | high | yes | timing measured |
| `bridge.telly` | Telly bridge | Bedrock + Java | scripted (needs live calibration) | high | yes | jump + place timing |
| `bridge.speed` | Speed bridge | Bedrock + Java | scripted (needs live calibration) | high | yes | timing measured |
| `bridge.breezily` | Breezily bridge | Bedrock + Java | scripted (needs live calibration) | high | yes | timing measured |
| `bridge.witch` | Witch bridge | Bedrock + Java | planned | high | yes | timing measured |
| `bridge.combat` | Combat bridge | Bedrock + Java | scripted (needs live calibration) | high | yes | blocks + enemy |
| `bridge.recovery` | Recovery bridge | Bedrock + Java | scripted (needs live calibration) | medium | no | blocks |
| `bridge.gap_fill` | Gap fill | Bedrock + Java | scripted (needs live calibration) | low | no | blocks |

## clutches

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `clutch.boat` | Boat clutch | Bedrock + Java | planned | medium | no | boat |
| `clutch.ladder_vine` | Ladder/vine clutch | Bedrock + Java | planned | medium | no | ladder/vine |
| `clutch.slime` | Slime clutch | Bedrock + Java | planned | medium | no | slime block |
| `clutch.cobweb` | Cobweb clutch | Bedrock + Java | planned | medium | no | cobweb |
| `clutch.hay_bale` | Hay-bale clutch | Bedrock + Java | planned | low | no | hay bale |
| `clutch.powder_snow` | Powder-snow clutch | Bedrock + Java | planned | medium | no | powder snow bucket |
| `clutch.honey` | Honey clutch | Bedrock + Java | planned | low | no | honey block |
| `clutch.scaffolding` | Scaffolding clutch | Bedrock + Java | planned | low | no | scaffolding |
| `clutch.wind_charge_cushion` | Wind-charge cushion | Bedrock + Java | scripted (needs live calibration) | medium | yes | wind charge, measured |
| `clutch.totem` | Totem save | Bedrock + Java | scripted (needs live calibration) | low | no | totem |

## survival

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `survival.sneak_halt` | Sneak halt | Bedrock + Java | executable (simulator-verified) | low | no | on ground |
| `survival.step_back` | Step back | Bedrock + Java | executable (simulator-verified) | low | no | on ground |
| `survival.bridge_forward` | Bridge forward to safety | Bedrock + Java | executable (simulator-verified) | low | no | blocks + clickable face |
| `survival.block_clutch` | Block clutch | Bedrock + Java | executable (simulator-verified) | medium | no | blocks + clickable face under the player |
| `survival.water_clutch` | Water-bucket clutch | Bedrock + Java | executable (simulator-verified) | medium | no | water bucket + known ground |
| `survival.pearl_escape` | Pearl escape | Bedrock + Java | executable (simulator-verified) | medium | yes | calibrated pearl + health > landing cost |
| `survival.lava_escape` | Lava escape | Bedrock + Java | scripted (needs live calibration) | high | no | in or next to lava |
| `survival.fire_extinguish` | Fire extinguish | Bedrock + Java | planned | medium | no | burning |
| `survival.drown_escape` | Drowning escape | Bedrock + Java | planned | medium | no | low air |
| `survival.suffocation_escape` | Suffocation escape | Bedrock + Java | planned | medium | no | inside a block |
| `survival.hazard_avoid` | Cactus/magma avoidance | Bedrock + Java | planned | low | no | harmful blocks |
| `survival.eat_manage` | Hunger management | Bedrock + Java | planned | low | no | food |
| `survival.golden_apple` | Golden-apple heal | Bedrock + Java | scripted (needs live calibration) | low | no | golden apple |
| `survival.potion_drink` | Potion drink | Bedrock + Java | scripted (needs live calibration) | low | no | healing/regeneration potion |
| `survival.armor_swap` | Armor swap | Bedrock + Java | planned | low | no | better armor |
| `survival.shield_raise` | Shield raise | Bedrock + Java | planned | low | no | shield |
| `survival.shelter_dig_in` | Dig-in shelter | Bedrock + Java | planned | low | no | tool |
| `survival.night_shelter` | Night shelter | Bedrock + Java | planned | low | no | blocks |
| `survival.storm_shelter` | Lightning shelter | Bedrock + Java | planned | low | no | thunderstorm |
| `survival.spawn_set` | Set spawn | Bedrock + Java | planned | low | no | bed placed by the human |
| `survival.mob_creeper` | Creeper handling | Bedrock + Java | planned | medium | no | creeper near |
| `survival.mob_skeleton` | Skeleton approach | Bedrock + Java | planned | medium | no | skeleton near |
| `survival.mob_warden` | Warden stealth | Bedrock + Java | planned | high | no | deep dark |
| `survival.end_edge` | End void edge | Bedrock + Java | planned | high | no | in the End |

## mining

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `mine.clear_path` | Clear path | Bedrock + Java | planned | low | no | tool |
| `mine.strip` | Strip mining | Bedrock + Java | planned | low | no | pickaxe |
| `mine.branch` | Branch mining | Bedrock + Java | planned | low | no | pickaxe |
| `mine.staircase_down` | Safe staircase down | Bedrock + Java | planned | low | no | pickaxe |
| `mine.lava_safe_dig` | Lava-safe digging | Bedrock + Java | planned | medium | no | pickaxe + water |
| `mine.gravity_watch` | Falling-block watch | Bedrock + Java | planned | low | no | sand/gravel above |
| `mine.ore_follow` | Ore vein follow | Bedrock + Java | planned | low | no | ore |
| `mine.tool_select` | Best tool | Bedrock + Java | planned | low | no | tools |

## building

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `build.cover_wall` | Cover wall | Bedrock + Java | scripted (needs live calibration) | low | no | blocks |
| `build.high_ground` | High ground | Bedrock + Java | scripted (needs live calibration) | low | no | blocks |
| `build.seal_corridor` | Seal corridor | Bedrock + Java | planned | low | no | blocks |
| `build.trap_pit` | Trap pit | Bedrock + Java | planned | medium | no | tools + blocks |
| `build.chase_barrier` | Chase barrier | Bedrock + Java | planned | low | no | blocks |
| `build.shelter_hut` | Hut shelter | Bedrock + Java | planned | low | no | blocks |
| `build.nether_portal` | Nether portal | Bedrock + Java | planned | medium | no | obsidian/flint or lava-cast |

## items

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `items.switch_best` | Best item switch | Bedrock + Java | scripted (needs live calibration) | low | no | inventory |
| `items.custom_probe` | Custom item probe | Bedrock + Java | scripted (needs live calibration) | medium | no | user consent + safe footing |
| `items.pearl_use` | Pearl use | Bedrock + Java | scripted (needs live calibration) | medium | yes | calibrated pearl |
| `items.wind_charge_use` | Wind-charge use | Bedrock + Java | scripted (needs live calibration) | medium | yes | wind charge |
| `items.fireball_use` | Fireball use | Bedrock + Java | planned | high | yes | fire charge |
| `items.potion_use` | Potion use | Bedrock + Java | scripted (needs live calibration) | low | no | potions |
| `items.lava_bucket` | Lava bucket use | Bedrock + Java | planned | high | no | lava + water |
| `items.flint_steel` | Flint and steel | Bedrock + Java | planned | medium | no | flint and steel |

## mobs

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `mob.hoglin_kite` | Hoglin kite | Bedrock + Java | planned | medium | no | hoglin |
| `mob.blaze_fight` | Blaze fight | Bedrock + Java | planned | high | no | nether fortress |
| `mob.ghast_deflect` | Ghast deflect | Bedrock + Java | planned | high | no | ghast |
| `mob.piglin_barter` | Piglin barter | Bedrock + Java | planned | low | no | gold, never economy servers |
| `mob.enderman_gaze` | Enderman gaze rule | Bedrock + Java | planned | medium | no | enderman |
| `mob.phantom_avoid` | Phantom avoidance | Bedrock + Java | planned | low | no | sleepless night |

## bosses

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `boss.dragon_crystals` | Dragon crystals | Bedrock + Java | planned | high | no | bow |
| `boss.dragon_perch` | Dragon perch melee | Bedrock + Java | planned | high | no | melee gear |
| `boss.wither_arena` | Wither arena | Bedrock + Java | planned | high | no | prepared arena |
| `boss.elder_guardian` | Elder guardian | Bedrock + Java | planned | medium | no | water breathing |

## speedrun

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `speed.bastion_route` | Bastion route | Bedrock + Java | planned | high | no | gold armor |
| `speed.stronghold_triangulate` | Stronghold triangulation | Bedrock + Java | planned | low | no | eyes of ender |
| `speed.lava_pool_portal` | Lava-pool portal | Bedrock + Java | planned | high | no | water + obsidian |
| `speed.end_entry` | End entry | Bedrock + Java | planned | high | no | stronghold |

## navigation

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `nav.path_ground` | Ground path planning | Bedrock + Java | planned | low | no | collision map |
| `nav.void_aware` | Void-aware routing | Bedrock + Java | planned | medium | no | world grid |

## perception

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `perception.hazard_scan` | Hazard look-ahead | Bedrock + Java | executable (simulator-verified) | low | no | world grid |
| `perception.opponent_model` | Opponent model | Bedrock + Java | executable (simulator-verified) | low | no | opponent samples |
| `perception.human_intent` | Human intent prediction | Bedrock + Java | executable (simulator-verified) | low | no | touch stream |
| `perception.item_learning` | Custom item learning | Bedrock + Java | executable (simulator-verified) | low | no | observed use |

## planning

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `plan.skill_rotation` | Skill rotation | Bedrock + Java | executable (simulator-verified) | low | no | outcomes |
| `plan.failure_transfer` | Failure transfer | Bedrock + Java | executable (simulator-verified) | low | no | outcomes |
| `plan.double_input_takeover` | Second-input takeover | Bedrock + Java | executable (simulator-verified) | low | no | touch stream |
| `plan.emergency_override` | Emergency override | Bedrock + Java | executable (simulator-verified) | low | no | guardian |

## blocked by policy

| id | technique | editions | maturity | risk | needs calibration | requires |
|---|---|---|---|---|---|---|
| `policy.bed_destruction` | Bed destruction | Bedrock + Java | blocked by policy | high | no | - |
| `policy.economy_automation` | Economy automation | Bedrock + Java | blocked by policy | high | no | - |
| `exploit.reach_extension` | Reach extension | Bedrock + Java | blocked by policy | high | no | - |
| `exploit.hitbox_expansion` | Hitbox expansion | Bedrock + Java | blocked by policy | high | no | - |
| `exploit.velocity_modification` | Velocity modification | Bedrock + Java | blocked by policy | high | no | - |
| `exploit.timer_speed` | Timer / tick manipulation | Bedrock + Java | blocked by policy | high | no | - |
| `exploit.packet_duplication` | Duplication / packet exploits | Bedrock + Java | blocked by policy | high | no | - |
| `exploit.fly_noclip` | Fly / noclip | Bedrock + Java | blocked by policy | high | no | - |
| `exploit.anti_cheat_bypass` | Anti-cheat bypass | Bedrock + Java | blocked by policy | high | no | - |
| `exploit.identity_spoofing` | Identity spoofing | Bedrock + Java | blocked by policy | high | no | - |
| `exploit.hidden_automation` | Hiding automation from a server owner | Bedrock + Java | blocked by policy | high | no | - |
