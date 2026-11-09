#include "core/technique_registry.hpp"

#include <algorithm>
#include <set>

namespace bedrockai {
namespace {

using F = TechFamily;
using M = Maturity;
using K = Risk;
constexpr std::uint8_t J = kJava;
constexpr std::uint8_t BJ = kBothEditions;
constexpr std::uint32_t PVP = kModePvP;
constexpr std::uint32_t BW = kModeBedWars;
constexpr std::uint32_t SUR = kModeSurvival;
constexpr std::uint32_t SPD = kModeSpeedrun;
constexpr std::uint32_t PK = kModeParkour;
constexpr std::uint32_t BLD = kModeBuild;
constexpr std::uint32_t BOS = kModeBoss;
constexpr std::uint32_t ANY = kModeAny;

// id, name, family, editions, maturity, risk, survival_critical, needs_calibration, modes, requires, summary
const std::vector<TechniqueSpec>& table() {
    static const std::vector<TechniqueSpec> t = {
        // ------------------------------------------------------------------ melee combat
        {"combat.strafe_orbit", "Strafe orbit", F::MeleeCombat, BJ, M::Executable, K::Medium, false, false, PVP | BW, "enemy in range", "circle the target, alternating direction, aiming at the predicted position"},
        {"combat.pressure_chase", "Pressure chase", F::MeleeCombat, BJ, M::Executable, K::Medium, false, false, PVP | BW, "enemy retreating or stationary", "close the distance and keep attacking"},
        {"combat.kite_back", "Kite back", F::MeleeCombat, BJ, M::Executable, K::Low, false, false, PVP | BW, "enemy approaching", "hit then back off to keep the enemy at the edge of reach"},
        {"combat.retreat_reset", "Retreat and reset", F::MeleeCombat, BJ, M::Executable, K::Low, true, false, PVP | BW | SUR, "health low or outnumbered", "break contact, recover, re-engage when risk drops"},
        {"combat.aim_lead", "Predictive aim", F::MeleeCombat, BJ, M::Scripted, K::Low, false, false, PVP | BW, "opponent tracked, live hit-cone measured", "aim at the learned future position (OpponentModel is verified; the simulator hit-cone is too forgiving to show the benefit)"},
        {"combat.target_switch", "Target switching", F::MeleeCombat, BJ, M::Scripted, K::Medium, false, false, PVP | BW, "two or more enemies", "re-score targets continuously, never tunnel on a low-health enemy while another can kill you"},
        {"combat.w_tap", "W-tap", F::MeleeCombat, BJ, M::Scripted, K::Low, false, true, PVP | BW, "measured sprint-reset timing", "release/re-press forward to reset sprint for extra knockback"},
        {"combat.s_tap", "S-tap", F::MeleeCombat, BJ, M::Scripted, K::Low, false, true, PVP | BW, "measured spacing", "tap back to widen spacing between hits"},
        {"combat.jump_reset", "Jump reset", F::MeleeCombat, BJ, M::Scripted, K::Low, false, true, PVP | BW, "knockback model measured", "jump as a hit lands to cut incoming knockback"},
        {"combat.crit_jump", "Critical-hit jump", F::MeleeCombat, BJ, M::Scripted, K::Low, false, true, PVP | BW, "falling-attack rule measured", "attack on the way down for a critical hit"},
        {"combat.combo_lock", "Combo lock", F::MeleeCombat, BJ, M::Scripted, K::Medium, false, true, PVP | BW, "hit cadence measured", "chain hits inside the enemy's knockback window"},
        {"combat.cps_pace", "Measured attack pacing", F::MeleeCombat, BJ, M::Scripted, K::Low, false, true, PVP | BW, "attack cadence from the measured profile", "never faster than the measured legal cadence"},
        {"combat.block_hit", "Block-hit", F::MeleeCombat, J, M::Planned, K::Low, false, true, PVP, "Java 1.8-style sword block", "sword-block between swings"},
        {"combat.hit_select", "Hit selection", F::MeleeCombat, J, M::Planned, K::Low, false, true, PVP, "attack-cooldown model", "swing only when the cooldown is full"},
        {"combat.sweep_edge", "Sweep edge", F::MeleeCombat, J, M::Planned, K::Low, false, true, PVP, "sweep rule measured", "use sweeping hits on groups"},
        {"combat.shield_bait", "Shield bait", F::MeleeCombat, BJ, M::Planned, K::Medium, false, false, PVP, "enemy with shield", "bait the raise then punish the lowering"},
        {"combat.axe_shield_break", "Axe shield break", F::MeleeCombat, BJ, M::Planned, K::Low, false, false, PVP, "axe in hotbar", "disable a raised shield with an axe"},
        {"combat.mace_smash", "Mace smash", F::MeleeCombat, BJ, M::Planned, K::High, false, true, PVP, "mace + height", "fall-height smash attack"},
        {"combat.spear_lunge", "Spear lunge", F::MeleeCombat, BJ, M::Planned, K::Medium, false, true, PVP, "spear item present, profile learned", "lunge attack with a spear-class item"},
        {"combat.wall_pin", "Wall pin", F::MeleeCombat, BJ, M::Planned, K::Medium, false, false, PVP, "wall behind enemy", "hold the enemy against a wall"},
        {"combat.edge_knock", "Edge knock", F::MeleeCombat, BJ, M::Planned, K::Medium, false, false, PVP | BW, "enemy near an edge", "knock the enemy toward a void edge while keeping own footing"},
        {"combat.jump_dodge", "Jump dodge", F::MeleeCombat, BJ, M::Planned, K::Low, false, false, PVP, "incoming melee", "sidestep or hop to avoid a swing"},
        {"combat.heal_trade", "Heal while trading", F::MeleeCombat, BJ, M::Scripted, K::Medium, true, false, PVP | BW, "golden apple/potion", "heal at the safest moment of a fight"},
        {"combat.block_cover", "Block cover", F::MeleeCombat, BJ, M::Scripted, K::Medium, false, false, PVP | BW, "blocks available", "place a block between you and incoming damage"},
        {"combat.rod_combo", "Rod combo", F::MeleeCombat, BJ, M::Planned, K::Medium, false, true, PVP, "fishing rod", "pull with the rod then hit"},
        // ------------------------------------------------------------------ ranged
        {"ranged.bow_lead_shot", "Bow lead shot", F::RangedCombat, BJ, M::Scripted, K::Low, false, true, PVP | SUR, "calibrated bow physics", "ballistic aim with target-velocity lead"},
        {"ranged.bow_charge", "Bow charge timing", F::RangedCombat, BJ, M::Scripted, K::Low, false, true, PVP | SUR, "measured charge curve", "release at full draw"},
        {"ranged.crossbow_burst", "Crossbow burst", F::RangedCombat, BJ, M::Planned, K::Low, false, true, PVP, "crossbow loaded", "pre-loaded burst on a closing target"},
        {"ranged.trident_throw", "Trident throw", F::RangedCombat, BJ, M::Planned, K::Low, false, true, PVP | SUR, "trident", "thrown trident with lead"},
        {"ranged.snowball_knock", "Snowball/egg knock", F::RangedCombat, BJ, M::Planned, K::Low, false, true, PVP | BW, "snowballs/eggs", "small knockback pokes to break a bridge or a stance"},
        {"ranged.fireball_punch", "Fireball punch", F::RangedCombat, BJ, M::Planned, K::Medium, false, true, PVP | BW, "fire charge", "deflect or launch fireballs at enemies or terrain"},
        {"ranged.wind_charge_knock", "Wind charge knock", F::RangedCombat, BJ, M::Scripted, K::Medium, false, true, PVP | BW, "wind charge, calibrated", "displace an enemy near an edge"},
        {"ranged.potion_throw", "Splash potion throw", F::RangedCombat, BJ, M::Planned, K::Low, false, true, PVP, "splash potions", "slow/poison/weakness at range"},
        {"ranged.cobweb_trap", "Cobweb trap", F::RangedCombat, BJ, M::Planned, K::Low, false, false, PVP | BW, "cobwebs", "slow a chaser or hold a target"},
        {"ranged.lava_trap", "Lava bucket trap", F::RangedCombat, BJ, M::Planned, K::High, false, false, PVP, "lava bucket + water to clean up", "area denial; must always be cleanable"},
        {"ranged.tnt_trap", "TNT trap", F::RangedCombat, BJ, M::Planned, K::High, false, false, PVP, "TNT + flint", "timed explosive; never against beds (policy)"},
        // ------------------------------------------------------------------ movement
        {"move.sprint_jump", "Sprint jump", F::Movement, BJ, M::Scripted, K::Low, false, true, ANY, "jump profile measured", "chain jumps while sprinting"},
        {"move.strafe_jump", "Strafe jump", F::Movement, BJ, M::Scripted, K::Low, false, true, PVP | BW | PK, "air-control measured", "air-strafe for speed and dodging"},
        {"move.sneak_edge_walk", "Sneak edge walk", F::Movement, BJ, M::Scripted, K::Low, false, false, ANY, "ground edge", "walk along an edge without falling"},
        {"move.parkour_gap1", "1-block gap jump", F::Movement, BJ, M::Scripted, K::Medium, false, true, PK | BW, "gap <= 1", "standing/running jump over one block"},
        {"move.parkour_gap2", "2-block gap jump", F::Movement, BJ, M::Scripted, K::Medium, false, true, PK | BW, "gap <= 2", "sprint jump over two blocks"},
        {"move.parkour_gap3", "3-block gap jump", F::Movement, BJ, M::Scripted, K::High, false, true, PK | BW, "gap <= 3 measured", "sprint jump over three blocks"},
        {"move.parkour_gap4", "4-block gap jump", F::Movement, BJ, M::Planned, K::High, false, true, PK, "gap <= 4 measured", "momentum/neo jump"},
        {"move.neo_jump", "Neo jump", F::Movement, BJ, M::Planned, K::High, false, true, PK, "wall + gap", "jump around a wall"},
        {"move.ladder_climb", "Ladder/vine climb", F::Movement, BJ, M::Planned, K::Low, false, false, SUR | SPD, "ladder or vine", "climb and descend safely"},
        {"move.scaffold_climb", "Scaffolding climb", F::Movement, BJ, M::Planned, K::Low, false, false, SUR | BLD, "scaffolding", "climb/descend scaffolding"},
        {"move.swim_sprint", "Swim sprint", F::Movement, BJ, M::Planned, K::Low, false, false, SUR | SPD, "water", "fast swimming and surfacing"},
        {"move.boat_ice", "Boat on ice", F::Movement, BJ, M::Planned, K::Low, false, false, SUR | SPD, "boat + ice", "fast travel"},
        {"move.elytra_fly", "Elytra flight", F::Movement, BJ, M::Planned, K::High, false, true, SUR | SPD, "elytra + fireworks", "controlled glide and landing"},
        {"move.minecart_boost", "Minecart ride", F::Movement, BJ, M::Planned, K::Low, false, false, SUR | SPD, "rails", "rail travel"},
        {"move.mount_ride", "Mount riding", F::Movement, BJ, M::Planned, K::Low, false, false, SUR, "horse/saddle", "ride and steer mounts"},
        {"move.slime_bounce", "Slime bounce", F::Movement, BJ, M::Planned, K::Low, false, false, ANY, "slime block", "bounce to cancel falls or gain height"},
        {"move.bubble_column", "Bubble column", F::Movement, BJ, M::Planned, K::Low, false, false, SUR | SPD, "bubble column", "ride up/down"},
        {"move.web_escape", "Web escape", F::Movement, BJ, M::Planned, K::Low, false, false, ANY, "inside cobweb", "cut or break out of cobweb quickly"},
        {"move.powder_snow_escape", "Powder snow escape", F::Movement, BJ, M::Planned, K::Medium, true, false, SUR, "powder snow", "climb out before freezing"},
        {"move.pearl_traverse", "Pearl traverse", F::Movement, BJ, M::Scripted, K::Medium, false, true, ANY, "calibrated pearl", "cross a gap or chase with a pearl"},
        {"move.wind_charge_launch", "Wind-charge launch", F::Movement, BJ, M::Scripted, K::Medium, false, true, ANY, "wind charge, measured impulse", "vertical/horizontal boost"},
        {"move.fireball_launch", "Fireball launch", F::Movement, BJ, M::Planned, K::High, false, true, BW, "fire charge", "self-launch with a fireball"},
        {"move.knockback_recover", "Knockback recovery", F::Movement, BJ, M::Scripted, K::Medium, true, false, ANY, "knocked airborne", "regain control and aim for a safe landing"},
        // ------------------------------------------------------------------ bridging
        {"bridge.sneak_straight", "Sneak bridge (straight)", F::Bridging, BJ, M::Executable, K::Low, false, false, BW | BLD | PK, "blocks + clickable face", "creep forward in sneak placing one block ahead"},
        {"bridge.sneak_diagonal", "Sneak bridge (diagonal)", F::Bridging, BJ, M::Executable, K::Low, false, false, BW | BLD, "blocks + clickable face", "alternate x/z steps to cross diagonally"},
        {"movement.pillar_up", "Pillar up", F::Bridging, BJ, M::Scripted, K::Low, false, false, BW | SUR | BLD, "blocks, open headroom", "jump and place under the feet to gain height"},
        {"bridge.staircase_up", "Staircase bridge up", F::Bridging, BJ, M::Scripted, K::Low, false, true, BW | BLD, "blocks", "diagonal ascent one block per step"},
        {"bridge.staircase_down", "Staircase bridge down", F::Bridging, BJ, M::Scripted, K::Low, false, true, BW | BLD, "blocks", "controlled descent"},
        {"bridge.ninja", "Ninja bridge", F::Bridging, BJ, M::Scripted, K::Medium, false, true, BW | PK, "backward sneak timing measured", "backward sneak-place at the edge"},
        {"bridge.god", "God bridge", F::Bridging, BJ, M::Scripted, K::High, false, true, BW | PK, "placement cadence measured", "fast backward bridge with diagonal placement"},
        {"bridge.moonwalk", "Moonwalk bridge", F::Bridging, BJ, M::Scripted, K::High, false, true, BW | PK, "timing measured", "backward walk bridge"},
        {"bridge.telly", "Telly bridge", F::Bridging, BJ, M::Scripted, K::High, false, true, BW | PK, "jump + place timing", "forward sprint-jump bridging"},
        {"bridge.speed", "Speed bridge", F::Bridging, BJ, M::Scripted, K::High, false, true, BW | PK, "timing measured", "sprint bridging"},
        {"bridge.breezily", "Breezily bridge", F::Bridging, BJ, M::Scripted, K::High, false, true, BW | PK, "timing measured", "alternating strafe bridge"},
        {"bridge.witch", "Witch bridge", F::Bridging, BJ, M::Planned, K::High, false, true, BW | PK, "timing measured", "diagonal-forward bridge variant"},
        {"bridge.combat", "Combat bridge", F::Bridging, BJ, M::Scripted, K::High, false, true, BW | PVP, "blocks + enemy", "bridge while defending"},
        {"bridge.recovery", "Recovery bridge", F::Bridging, BJ, M::Scripted, K::Medium, true, false, BW | SUR, "blocks", "rebuild a path back after a knockback"},
        {"bridge.gap_fill", "Gap fill", F::Bridging, BJ, M::Scripted, K::Low, false, false, BW | BLD, "blocks", "fill a known gap in the route"},
        // ------------------------------------------------------------------ clutches
        {"clutch.boat", "Boat clutch", F::Clutch, BJ, M::Planned, K::Medium, true, false, SUR | SPD, "boat", "place a boat under a fall"},
        {"clutch.ladder_vine", "Ladder/vine clutch", F::Clutch, BJ, M::Planned, K::Medium, true, false, SUR, "ladder/vine", "grab to stop a fall"},
        {"clutch.slime", "Slime clutch", F::Clutch, BJ, M::Planned, K::Medium, true, false, ANY, "slime block", "cancel fall damage"},
        {"clutch.cobweb", "Cobweb clutch", F::Clutch, BJ, M::Planned, K::Medium, true, false, ANY, "cobweb", "cancel fall damage"},
        {"clutch.hay_bale", "Hay-bale clutch", F::Clutch, BJ, M::Planned, K::Low, true, false, SUR, "hay bale", "reduce fall damage"},
        {"clutch.powder_snow", "Powder-snow clutch", F::Clutch, BJ, M::Planned, K::Medium, true, false, SUR, "powder snow bucket", "cushion a fall"},
        {"clutch.honey", "Honey clutch", F::Clutch, BJ, M::Planned, K::Low, true, false, SUR, "honey block", "slow a fall"},
        {"clutch.scaffolding", "Scaffolding clutch", F::Clutch, BJ, M::Planned, K::Low, true, false, BLD | SUR, "scaffolding", "cushion/catch"},
        {"clutch.wind_charge_cushion", "Wind-charge cushion", F::Clutch, BJ, M::Scripted, K::Medium, true, true, ANY, "wind charge, measured", "cancel downward speed"},
        {"clutch.totem", "Totem save", F::Clutch, BJ, M::Scripted, K::Low, true, false, ANY, "totem", "hold the totem when death is likely"},
        // ------------------------------------------------------------------ survival
        {"survival.sneak_halt", "Sneak halt", F::Survival, BJ, M::Executable, K::Low, true, false, ANY, "on ground", "stop and sneak before an edge or hazard"},
        {"survival.step_back", "Step back", F::Survival, BJ, M::Executable, K::Low, true, false, ANY, "on ground", "reverse the input when too close to stop"},
        {"survival.bridge_forward", "Bridge forward to safety", F::Survival, BJ, M::Executable, K::Low, true, false, ANY, "blocks + clickable face", "sneak-creep and place the missing block under the route"},
        {"survival.block_clutch", "Block clutch", F::Survival, BJ, M::Executable, K::Medium, true, false, ANY, "blocks + clickable face under the player", "place a block under a falling player"},
        {"survival.water_clutch", "Water-bucket clutch", F::Survival, BJ, M::Executable, K::Medium, true, false, ANY, "water bucket + known ground", "pour water on the landing block"},
        {"survival.pearl_escape", "Pearl escape", F::Survival, BJ, M::Executable, K::Medium, true, true, ANY, "calibrated pearl + health > landing cost", "teleport to a known safe landing"},
        {"survival.lava_escape", "Lava escape", F::Survival, BJ, M::Scripted, K::High, true, false, ANY, "in or next to lava", "leave lava, use water or fire resistance"},
        {"survival.fire_extinguish", "Fire extinguish", F::Survival, BJ, M::Planned, K::Medium, true, false, ANY, "burning", "water or leave the fire"},
        {"survival.drown_escape", "Drowning escape", F::Survival, BJ, M::Planned, K::Medium, true, false, SUR, "low air", "surface or build an air pocket"},
        {"survival.suffocation_escape", "Suffocation escape", F::Survival, BJ, M::Planned, K::Medium, true, false, SUR, "inside a block", "mine out safely"},
        {"survival.hazard_avoid", "Cactus/magma avoidance", F::Survival, BJ, M::Planned, K::Low, true, false, SUR, "harmful blocks", "route around damaging blocks"},
        {"survival.eat_manage", "Hunger management", F::Survival, BJ, M::Planned, K::Low, false, false, SUR | SPD, "food", "eat before it matters"},
        {"survival.golden_apple", "Golden-apple heal", F::Survival, BJ, M::Scripted, K::Low, true, false, PVP | BW | SUR, "golden apple", "heal at a safe window"},
        {"survival.potion_drink", "Potion drink", F::Survival, BJ, M::Scripted, K::Low, true, false, ANY, "healing/regeneration potion", "drink when it prevents death"},
        {"survival.armor_swap", "Armor swap", F::Survival, BJ, M::Planned, K::Low, false, false, ANY, "better armor", "equip the better piece"},
        {"survival.shield_raise", "Shield raise", F::Survival, BJ, M::Planned, K::Low, true, false, PVP | SUR, "shield", "block incoming melee/arrows"},
        {"survival.shelter_dig_in", "Dig-in shelter", F::Survival, BJ, M::Planned, K::Low, true, false, SUR, "tool", "dig into a hillside to hide"},
        {"survival.night_shelter", "Night shelter", F::Survival, BJ, M::Planned, K::Low, false, false, SUR, "blocks", "enclose and wait"},
        {"survival.storm_shelter", "Lightning shelter", F::Survival, BJ, M::Planned, K::Low, false, false, SUR, "thunderstorm", "avoid metal/open ground"},
        {"survival.spawn_set", "Set spawn", F::Survival, BJ, M::Planned, K::Low, false, false, SUR, "bed placed by the human", "use (never destroy) a bed to set the spawn"},
        {"survival.mob_creeper", "Creeper handling", F::Survival, BJ, M::Planned, K::Medium, true, false, SUR, "creeper near", "hit and back off, never stand in the blast"},
        {"survival.mob_skeleton", "Skeleton approach", F::Survival, BJ, M::Planned, K::Medium, false, false, SUR, "skeleton near", "zig-zag approach or shield"},
        {"survival.mob_warden", "Warden stealth", F::Survival, BJ, M::Planned, K::High, true, false, SUR, "deep dark", "sneak, avoid vibrations, retreat"},
        {"survival.end_edge", "End void edge", F::Survival, BJ, M::Planned, K::High, true, false, SUR | SPD | BOS, "in the End", "keep away from the void, bridge out"},
        // ------------------------------------------------------------------ mining
        {"mine.clear_path", "Clear path", F::Mining, BJ, M::Planned, K::Low, false, false, SUR | SPD, "tool", "mine obstructing ordinary blocks"},
        {"mine.strip", "Strip mining", F::Mining, BJ, M::Planned, K::Low, false, false, SUR, "pickaxe", "systematic ore search"},
        {"mine.branch", "Branch mining", F::Mining, BJ, M::Planned, K::Low, false, false, SUR, "pickaxe", "side tunnels from a spine"},
        {"mine.staircase_down", "Safe staircase down", F::Mining, BJ, M::Planned, K::Low, false, false, SUR, "pickaxe", "descend without digging straight down"},
        {"mine.lava_safe_dig", "Lava-safe digging", F::Mining, BJ, M::Planned, K::Medium, true, false, SUR, "pickaxe + water", "check liquids ahead before breaking"},
        {"mine.gravity_watch", "Falling-block watch", F::Mining, BJ, M::Planned, K::Low, true, false, SUR, "sand/gravel above", "avoid suffocation"},
        {"mine.ore_follow", "Ore vein follow", F::Mining, BJ, M::Planned, K::Low, false, false, SUR, "ore", "follow a vein"},
        {"mine.tool_select", "Best tool", F::Mining, BJ, M::Planned, K::Low, false, false, ANY, "tools", "switch to the fastest/safest tool"},
        // ------------------------------------------------------------------ building
        {"build.cover_wall", "Cover wall", F::Building, BJ, M::Scripted, K::Low, false, false, PVP | BW, "blocks", "quick wall against ranged damage"},
        {"build.high_ground", "High ground", F::Building, BJ, M::Scripted, K::Low, false, false, PVP | BW, "blocks", "gain height over an enemy"},
        {"build.seal_corridor", "Seal corridor", F::Building, BJ, M::Planned, K::Low, false, false, PVP | BW, "blocks", "block a choke point"},
        {"build.trap_pit", "Trap pit", F::Building, BJ, M::Planned, K::Medium, false, false, PVP | SUR, "tools + blocks", "pit/trap for mobs or players"},
        {"build.chase_barrier", "Chase barrier", F::Building, BJ, M::Planned, K::Low, false, false, PVP | BW, "blocks", "slow a chaser"},
        {"build.shelter_hut", "Hut shelter", F::Building, BJ, M::Planned, K::Low, false, false, SUR | BLD, "blocks", "small safe room"},
        {"build.nether_portal", "Nether portal", F::Building, BJ, M::Planned, K::Medium, false, false, SPD | SUR, "obsidian/flint or lava-cast", "build or cast a portal"},
        // ------------------------------------------------------------------ items
        {"items.switch_best", "Best item switch", F::Items, BJ, M::Scripted, K::Low, false, false, ANY, "inventory", "pick the best item for the situation and verify the switch"},
        {"items.custom_probe", "Custom item probe", F::Items, BJ, M::Scripted, K::Medium, false, false, ANY, "user consent + safe footing", "learn an unknown item from observed effects (ItemLearner)"},
        {"items.pearl_use", "Pearl use", F::Items, BJ, M::Scripted, K::Medium, false, true, ANY, "calibrated pearl", "throw with solved aim"},
        {"items.wind_charge_use", "Wind-charge use", F::Items, BJ, M::Scripted, K::Medium, false, true, ANY, "wind charge", "use for displacement/recovery"},
        {"items.fireball_use", "Fireball use", F::Items, BJ, M::Planned, K::High, false, true, BW, "fire charge", "terrain/enemy displacement"},
        {"items.potion_use", "Potion use", F::Items, BJ, M::Scripted, K::Low, false, false, ANY, "potions", "drink or throw by value"},
        {"items.lava_bucket", "Lava bucket use", F::Items, BJ, M::Planned, K::High, false, false, SUR | PVP, "lava + water", "controlled lava use"},
        {"items.flint_steel", "Flint and steel", F::Items, BJ, M::Planned, K::Medium, false, false, SUR | SPD, "flint and steel", "light portals/tnt/fire"},
        // ------------------------------------------------------------------ mobs / bosses / speedrun
        {"mob.hoglin_kite", "Hoglin kite", F::Mobs, BJ, M::Planned, K::Medium, false, false, SUR | SPD, "hoglin", "keep range"},
        {"mob.blaze_fight", "Blaze fight", F::Mobs, BJ, M::Planned, K::High, false, false, SUR | SPD, "nether fortress", "snowballs/cover, collect rods"},
        {"mob.ghast_deflect", "Ghast deflect", F::Mobs, BJ, M::Planned, K::High, false, false, SUR | SPD, "ghast", "deflect fireballs"},
        {"mob.piglin_barter", "Piglin barter", F::Mobs, BJ, M::Planned, K::Low, false, false, SPD, "gold, never economy servers", "barter in vanilla survival only"},
        {"mob.enderman_gaze", "Enderman gaze rule", F::Mobs, BJ, M::Planned, K::Medium, true, false, SUR | SPD, "enderman", "avoid eye contact or fight with a block over the head"},
        {"mob.phantom_avoid", "Phantom avoidance", F::Mobs, BJ, M::Planned, K::Low, false, false, SUR, "sleepless night", "shelter or fight on a roof"},
        {"boss.dragon_crystals", "Dragon crystals", F::Boss, BJ, M::Planned, K::High, false, false, BOS | SPD, "bow", "snipe end crystals"},
        {"boss.dragon_perch", "Dragon perch melee", F::Boss, BJ, M::Planned, K::High, false, false, BOS | SPD, "melee gear", "melee when it lands on the fountain"},
        {"boss.wither_arena", "Wither arena", F::Boss, BJ, M::Planned, K::High, false, false, BOS, "prepared arena", "fight in a sheltered arena"},
        {"boss.elder_guardian", "Elder guardian", F::Boss, BJ, M::Planned, K::Medium, false, false, BOS, "water breathing", "monument fight"},
        {"speed.bastion_route", "Bastion route", F::Speedrun, BJ, M::Planned, K::High, false, false, SPD, "gold armor", "route through a bastion"},
        {"speed.stronghold_triangulate", "Stronghold triangulation", F::Speedrun, BJ, M::Planned, K::Low, false, false, SPD, "eyes of ender", "throw-and-measure navigation"},
        {"speed.lava_pool_portal", "Lava-pool portal", F::Speedrun, BJ, M::Planned, K::High, false, false, SPD, "water + obsidian", "cast a portal in a lava pool"},
        {"speed.end_entry", "End entry", F::Speedrun, BJ, M::Planned, K::High, false, false, SPD, "stronghold", "enter and prepare"},
        // ------------------------------------------------------------------ navigation / perception / planning
        {"nav.path_ground", "Ground path planning", F::Navigation, BJ, M::Planned, K::Low, false, false, ANY, "collision map", "A* on verified collision data"},
        {"nav.void_aware", "Void-aware routing", F::Navigation, BJ, M::Planned, K::Medium, true, false, ANY, "world grid", "never route across unknown/void cells"},
        {"perception.hazard_scan", "Hazard look-ahead", F::Perception, BJ, M::Executable, K::Low, true, false, ANY, "world grid", "predict void/lava/lethal falls along the expected path"},
        {"perception.opponent_model", "Opponent model", F::Perception, BJ, M::Executable, K::Low, false, false, PVP | BW, "opponent samples", "classify the style and predict its movement rhythm"},
        {"perception.human_intent", "Human intent prediction", F::Perception, BJ, M::Executable, K::Low, false, false, ANY, "touch stream", "recognise attack/place/move intent and heading habits"},
        {"perception.item_learning", "Custom item learning", F::Perception, BJ, M::Executable, K::Low, false, false, ANY, "observed use", "infer capabilities from observed effects, never from names"},
        {"plan.skill_rotation", "Skill rotation", F::Planning, BJ, M::Executable, K::Low, false, false, ANY, "outcomes", "Thompson sampling with cooldowns, bans and repetition penalty"},
        {"plan.failure_transfer", "Failure transfer", F::Planning, BJ, M::Executable, K::Low, false, false, ANY, "outcomes", "a failure against A is strong evidence vs A, weak vs B"},
        {"plan.double_input_takeover", "Second-input takeover", F::Planning, BJ, M::Executable, K::Low, false, false, ANY, "touch stream", "first counter-input ignored, second hands control to the human"},
        {"plan.emergency_override", "Emergency override", F::Planning, BJ, M::Executable, K::Low, true, false, ANY, "guardian", "survival overrides the human, except the explicit override button"},
        // ------------------------------------------------------------------ blocked by policy
        {"policy.bed_destruction", "Bed destruction", F::Blocked, BJ, M::Blocked, K::High, false, false, ANY, "-", "never: the AI does not break beds by any method, including explosive beds"},
        {"policy.economy_automation", "Economy automation", F::Blocked, BJ, M::Blocked, K::High, false, false, ANY, "-", "never: shops, buying, upgrades, trading and inventory economy stay human"},
        {"exploit.reach_extension", "Reach extension", F::Blocked, BJ, M::Blocked, K::High, false, false, ANY, "-", "never: reach beyond the game's rules"},
        {"exploit.hitbox_expansion", "Hitbox expansion", F::Blocked, BJ, M::Blocked, K::High, false, false, ANY, "-", "never"},
        {"exploit.velocity_modification", "Velocity modification", F::Blocked, BJ, M::Blocked, K::High, false, false, ANY, "-", "never: anti-knockback or speed beyond normal movement"},
        {"exploit.timer_speed", "Timer / tick manipulation", F::Blocked, BJ, M::Blocked, K::High, false, false, ANY, "-", "never"},
        {"exploit.packet_duplication", "Duplication / packet exploits", F::Blocked, BJ, M::Blocked, K::High, false, false, ANY, "-", "never"},
        {"exploit.fly_noclip", "Fly / noclip", F::Blocked, BJ, M::Blocked, K::High, false, false, ANY, "-", "never"},
        {"exploit.anti_cheat_bypass", "Anti-cheat bypass", F::Blocked, BJ, M::Blocked, K::High, false, false, ANY, "-", "never: no evasion of moderation, rate limits or bans"},
        {"exploit.identity_spoofing", "Identity spoofing", F::Blocked, BJ, M::Blocked, K::High, false, false, ANY, "-", "never"},
        {"exploit.hidden_automation", "Hiding automation from a server owner", F::Blocked, BJ, M::Blocked, K::High, false, false, ANY, "-", "never"},
    };
    return t;
}

} // namespace

const char* family_name(TechFamily f) {
    switch (f) {
        case TechFamily::MeleeCombat: return "melee combat";
        case TechFamily::RangedCombat: return "ranged combat";
        case TechFamily::Movement: return "movement";
        case TechFamily::Bridging: return "bridging";
        case TechFamily::Clutch: return "clutches";
        case TechFamily::Survival: return "survival";
        case TechFamily::Mining: return "mining";
        case TechFamily::Building: return "building";
        case TechFamily::Items: return "items";
        case TechFamily::Mobs: return "mobs";
        case TechFamily::Boss: return "bosses";
        case TechFamily::Speedrun: return "speedrun";
        case TechFamily::Navigation: return "navigation";
        case TechFamily::Perception: return "perception";
        case TechFamily::Planning: return "planning";
        case TechFamily::Blocked: return "blocked by policy";
    }
    return "";
}

const char* maturity_name(Maturity m) {
    switch (m) {
        case Maturity::Executable: return "executable (simulator-verified)";
        case Maturity::Scripted: return "scripted (needs live calibration)";
        case Maturity::Planned: return "planned";
        case Maturity::Blocked: return "blocked by policy";
    }
    return "";
}

TechniqueRegistry::TechniqueRegistry() : specs_(table()) {}

const TechniqueRegistry& TechniqueRegistry::instance() {
    static const TechniqueRegistry r;
    return r;
}

const TechniqueSpec* TechniqueRegistry::find(const std::string& id) const {
    for (const TechniqueSpec& s : specs_)
        if (id == s.id) return &s;
    return nullptr;
}

std::vector<const TechniqueSpec*> TechniqueRegistry::filter(std::uint8_t edition_mask, std::uint32_t mode_mask,
                                                            Maturity at_most) const {
    std::vector<const TechniqueSpec*> out;
    for (const TechniqueSpec& s : specs_) {
        if (s.maturity == Maturity::Blocked) continue;
        if (static_cast<int>(s.maturity) > static_cast<int>(at_most)) continue;
        if ((s.editions & edition_mask) == 0U) continue;
        if ((s.modes & mode_mask) == 0U) continue;
        out.push_back(&s);
    }
    return out;
}

TechniqueRegistry::Counts TechniqueRegistry::counts() const {
    Counts c;
    c.total = specs_.size();
    for (const TechniqueSpec& s : specs_) {
        switch (s.maturity) {
            case Maturity::Executable: ++c.executable; break;
            case Maturity::Scripted: ++c.scripted; break;
            case Maturity::Planned: ++c.planned; break;
            case Maturity::Blocked: ++c.blocked; break;
        }
    }
    return c;
}

bool TechniqueRegistry::blocked_by_policy(const std::string& id) const {
    const TechniqueSpec* s = find(id);
    return s != nullptr && s->maturity == Maturity::Blocked;
}

bool TechniqueRegistry::validate(std::string* error) const {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    std::set<std::string> ids;
    for (const TechniqueSpec& s : specs_) {
        if (!s.id || !*s.id || !s.name || !*s.name || !s.summary || !*s.summary) return fail("empty field in catalogue");
        if (!ids.insert(s.id).second) return fail(std::string("duplicate id: ") + s.id);
        if ((s.maturity == Maturity::Blocked) != (s.family == TechFamily::Blocked))
            return fail(std::string("blocked entries must use the Blocked family: ") + s.id);
        if (s.editions == 0U || (s.editions & ~kBothEditions) != 0U) return fail(std::string("bad edition mask: ") + s.id);
        if (s.maturity == Maturity::Executable && s.needs_calibration && std::string(s.id) != "survival.pearl_escape")
            return fail(std::string("executable entries cannot depend on calibration (except the pearl, which refuses when uncalibrated): ") + s.id);
    }
    return true;
}

} // namespace bedrockai
