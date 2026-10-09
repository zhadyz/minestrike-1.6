# MineStrike 1.6: manual

Minecraft built into Counter-Strike 1.6. de_dust2 is rebuilt from 123 × 141 × 37 Minecraft blocks: you can
mine it, build on it, blow craters in it and open its doors. You fight bots with diamond and netherite gear,
pick up their XP orbs, and fly over Dust II on an elytra boosted by fireworks. Everything Minecraft sounds,
looks and calculates like Minecraft, running inside the GoldSrc engine.

The mod was built in one unsupervised session by Claude (Anthropic), as a test of how far an AI agent can
take a game-modding project. It covers reverse engineering, a C++ engine mod, an asset pipeline, map
conversion, bot navigation, and testing with real keyboard and mouse input.

## Play

Double-click one of these in the repository root:

| Launcher | What you get |
|---|---|
| `PLAY_MineStrike_dust2.bat` | **de_dust2_mc**: the real de_dust2 with Minecraft built in (dig, build, redstone, CS rounds) |
| `PLAY_Minecraft_dust2.bat` | **mc_dust2**, the all-block Dust II, with 6 bots |
| `PLAY_classic_dust2_with_Minecraft_items.bat` | Regular de_dust2 with the Minecraft items, HUD and players |

Both run the private copy of the game in `game/Half-Life` in LAN/insecure mode, fully offline. Don't
connect this copy to online servers.

You get the MOTD with the controls, then the team menu. Pick a side, then press **B**.

### Controls

| Key | Action |
|---|---|
| **B** | The buy menu: Counter-Strike's guns and the Minecraft items, each with its price (free in creative mode). Click an item to buy it (a stackable one: as many as you can pay for; right click: one). The **Kits** tab buys whole sets. Tooltips show Minecraft's attribute lines, such as "When in Main Hand: 7 Attack Damage". The button at the bottom opens Counter-Strike's own buy menu. **B**, **E** or **Esc** closes it. |
| **1-5** | Your Counter-Strike weapons (they are slots 1-5 of the Minecraft hotbar) |
| **6-9** / mouse wheel | Minecraft hotbar slots |
| **Left click** | Attack (Minecraft cooldown, shown under the crosshair; crits while falling; sweeps). Hold it to mine a block. |
| **Right click** | Use: place blocks, open and close doors, equip armor, fire rockets, eat, throw pearls and XP bottles, draw the bow, light TNT or start a fire with flint and steel |
| **Jump while falling** (elytra on) | Glide. Right-click a firework rocket while gliding to boost. |
| **G** | Drop the held Minecraft item |
| **F5** | Minecraft camera: first person, third person behind, third person in front (`thirdperson` and `firstperson` work too) |
| Chat `/give <item> [n]`, `/kit`, `/gamemode creative`, `/xp 30 levels`, `/clear`, `/kill`, `/help` | Minecraft-style commands |

### What's in it

- **Minecraft combat.** Weapons use the real attack damage and attack-speed tables: wood → netherite swords
  and axes, the mace (smash attack scales with fall height), bow and arrows. Cooldown, crits, sweep and
  knockback follow Minecraft's formulas.
- **Armor.** Leather through netherite, plus the turtle helmet. Damage reduction uses Minecraft's
  armor/toughness formula, and armor wears out. The pieces render on your hands and on every player model.
  Bots sometimes spawn wearing armor.
- **XP.** Killed players and bots drop experience orbs that split like Minecraft's, float toward you, and
  give XP with the orb pickup and level-up sounds. Ore blocks drop XP too.
- **Elytra and fireworks.** The exact glide physics from Minecraft's `LivingEntity.travel`, plus firework
  boosting and the elytra wind sound.
- **Voxel Dust II (mc_dust2).** Converted from the real de_dust2 BSP. It has slabs and stairs where the map
  has ramps, ores underground and bedrock at the bottom. Blocks break at Minecraft's break times (tool tier
  and material matter), with crack stages, particles and drops. You can place blocks and open working doors.
  TNT uses Minecraft's ray-cast explosion, and HE grenades and C4 also blow craters. There's a Minecraft
  sky, clouds, smooth lighting and fog. The world resets each round (`mc_world_reset`).
- **Bots navigate the block world.** Their nav mesh is generated from the voxels and they handle 20-unit
  slab steps. They show up as Minecraft characters: CTs get player skins, Ts are zombies, husks and drowned.
  Their CS guns render as crossbows and bows.
- **Creeper.** A bot named *Creeper* plays the mob. It only carries a knife, so it hunts you down. Within 3
  blocks it hisses, swells, flashes white and explodes after 1.5 s, using Minecraft's explosion damage and
  knockback.
- **Enderman.** The *Enderman* bot is the real model with glowing eyes. Like the mob, it dodges projectiles:
  every bullet or arrow teleports it away in a burst of portal particles, so you have to kill it in melee.
  It drops ender pearls.
- **Mob details.** Bots have mob voices and drops: rotten flesh for zombies, gunpowder for creepers. A bot
  named *Dinnerbone* is drawn upside down, like any mob given that name in Minecraft.
- **Advancements and death messages.** Real Minecraft advancements pop up as toasts with their sounds, and
  chat announces them: Monster Hunter, Stone Age, Diamonds!, Suit Up, Cover Me with Diamonds, Cover Me in
  Debris, Sky's the Limit, Postmortal, Take Aim, Sniper Duel and Overkill. Every death gets Minecraft's
  wording, such as "was slain by", "was shot by", "was blown up by Creeper" or "hit the ground too hard".
- **Hit boxes true to the models.** Bullets are tested against the cubes you see, not against the hidden
  Counter-Strike model: a head shot is a shot at the drawn head, whether that is Steve's (twice as wide as a
  CS head), a creeper's (a foot lower) or an enderman's (a whole head above where a CS model ends), and a
  shot between an enderman's legs is a miss. Bots aim at the drawn head or chest. `mc_hitbox_show 1`
  outlines the boxes of the model you aim at (red head, yellow chest, orange stomach, blue arms, green legs).
- **Armor wear.** Armor loses durability by Minecraft's formula, at most once per half second (Minecraft's
  invulnerability window) however many bullets land in it; every bullet is still reduced by the armor.
- **Fire.** Flint and steel lights a fire on a floor or against anything that burns: planks, logs, slabs,
  stairs, wooden doors, wool, leaves, hay, and on the real de_dust2 the crates and wooden doors. It follows
  Minecraft's rules: a fire ticks every 1.5 to 2 seconds, each tick gives a plank-like neighbour 20 chances
  in 300 to burn out and nearby air a couple of chances in a hundred to catch, so it takes hold slowly and a
  stack of crates burns for minutes. TNT is primed. Standing in fire burns half a heart every half second,
  and you stay alight for 8 seconds after stepping out; the kill goes to whoever lit it.
- **Torches and light.** Torches (in the Blocks tab) stand on a floor or lean from a wall, and pop off when
  what holds them is removed. Fire, torches, glowstone and lit redstone lamps light their surroundings the
  way Minecraft does: full strength at the source, one level less per block, stopped by walls, warm in
  colour. On the real de_dust2 this lifts whatever is in shade (the tunnels most of all) and leaves
  daylight as it is.
- **The buy menu and the economy.** **B** opens one shop on every map: a Counter-Strike tab with the guns
  and equipment your team can buy at Counter-Strike's prices, and the Minecraft tabs, where every item
  carries its price. Your money is in the title bar; a price is green when you can pay it. Minecraft items
  are bought under Counter-Strike's rules (in a buy zone, during buy time) with the same money. A click on
  a stackable item buys as many as you can pay for, a right-click one. A full iron set is $1000, diamond
  $1800, netherite $2800, split over the pieces; swords and axes $100 to $800; a bow $400, a crossbow $650,
  arrows $5; a golden apple $250, a totem $1200. Blocks are dear, because one set down is cover, a wall, a
  step or a bridge for the rest of the round, and a full block costs by what it stops (see *Blocks in a
  round* below): $50 for what stops no bullet (wool, glass, sand, hay), $100 for wood (planks, logs,
  bookshelves), $200 for stone and its like, $300 for the harder ones, obsidian $800 (two at most,
  `mc_obsidian_max`). A slab is half its block, stairs three quarters, iron bars $150, a wooden door $150,
  an iron door $300, TNT $2000; plants, torches and redstone parts cost next to nothing. A block's tooltip
  says what it stops. What you mine from the map yourself is free. Kits cost the sum of their items.
  Creative mode (`/gamemode creative`) is free, and so is everything with `mc_economy 0`. Bots buy from the
  same list, one item at a time, with a third to two thirds of their money, and keep what they carry only
  if they live.
- **Blocks in a round.** A wall buys seconds, so the clocks are tight: a round of two and a half minutes,
  ten seconds of freeze, half a minute of buying, forty seconds on the bomb (`csmc.cfg`). What a block is
  made of decides what it is good for:

  | Block | Price | A bullet | Taken down by |
  |---|---|---|---|
  | Wool, hay, sand, gravel | $50 | goes through as if nothing were there | bare hands in about a second; wool and hay burn |
  | Glass | $50 | goes through, and the glass shatters with a crash | anything |
  | Iron bars | $150 | goes through; nobody walks through | a pickaxe |
  | Planks, logs, bookshelves, wooden doors | $100 | Counter-Strike's rule for wood: pistols, submachine guns and buckshot stop; rifles and the Deagle come through with about 60% of their damage | an axe (0.4 to 1.5 s); 3 s by hand **or with a pickaxe**; fire |
  | Stone, cobblestone, sandstone, bricks | $200 | stops, whatever the gun | a pickaxe (about a second); 7.5 s by hand; TNT |
  | Obsidian | $800, two at most | stops | 9.4 s with a diamond pickaxe, 37.5 s with a wooden one. TNT does not move it |

  So wool hides and does not protect; glass is an alarm; wood is the cheap wall, as good as stone against a
  side on pistols, the one wall that can be set alight, and the one a pickaxe is the wrong tool for; stone
  is the safe wall. (On the all-block map, sand is the ground and stops bullets.) Sand and gravel fall when
  what holds them is taken away: a column over a doorway, standing on one block, shuts it when that block
  goes. Three rules keep it a game:
  - *A block of your own side comes away in your hands at once*, whatever it is made of, and lies there
    to be picked up again. Nobody is walled in by a teammate, and a builder can open his own wall again.
    The other side has to dig.
  - *A planted bomb keeps the space around it free*: its own column and the eight around it, three cells
    high. What stands there when it is planted comes down, and nothing can be set there afterwards. A
    wall further out is a wall like any other.
  - *No building within 320 units of the other side's spawn points in the first 25 seconds of a round*
    (`mc_spawn_guard`, `mc_spawn_guard_time`).
- **The iron golem and the wither.** Two mobs a player builds the way Minecraft has it, from parts bought
  in the Blocks tab. Whoever sets the last part down gets it for his side: it fights the other side and
  leaves its own alone. Neither is a wander-and-attack mob. Each is a Counter-Strike bot made on the spot,
  so it knows the map and the round, and it plays the objective: on the Counter-Terrorist side with no bomb
  down it holds the bomb site the Terrorists are expected at; on the Terrorist side it goes with the bomb
  carrier and takes the site with him; with the bomb planted it goes to the bomb and stays on it, to guard
  it or to clear the way for the defuser. It does not carry, plant or defuse the bomb, does not count as a
  living member of its side when the round is decided, and is gone when the round is over.
  - *Iron golem*, $3,500: four iron blocks ($300 each) in a T (one on the ground, one on it, one to either
    side of that) and a carved pumpkin ($2,300) set on top last. Minecraft's numbers: 100 health (500
    here), 7.5 to 21.5 a blow (38 to 108), a blow a second, and a blow throws you into the air. It walks
    at 190 units a second where you run at 250. **Nothing hurts it but a sword.** A bullet does nothing to
    it and comes back at whoever fired it, with all it had; arrows, knives, TNT, fire and falls do nothing.
    (Another mob's blow does.) So you bring swords, or you go round it. One a side at a time
    (`mc_golem_max`).
  - *Wither*, $16,000, all the money a player can hold: four soul sand ($250 each) in the same T and three
    wither skeleton skulls ($5,000 each) along its top, the last skull set last. Everybody is told at once.
    It rises for eleven seconds where it was built and nothing touches it then; then it goes off with a
    blast of power 7 that digs a crater five blocks deep and that the whole map hears, and is loose with
    Minecraft's 300 health (1,500 here), a point of it back every second. Whoever of the other side it sees
    within 1,500 units gets a skull every two seconds: 40 damage and the Wither effect on a hit (5 health a
    second for ten seconds), and a small blast where it lands. Blocks in its way get a blue skull, whose
    blast takes any block but bedrock, obsidian included. A second after it is hurt the blocks around its
    body go, the map's own too. Bullets hurt it; below half its health arrows do nothing and bullets half.
    It hangs in the air over holes. A side may raise one every eight rounds (`mc_wither_rounds`).
  - *Bots.* A bot with a rifle's money besides sometimes buys a golem's parts and builds it at the first
    quiet moment of the round (`mc_bot_golem`); one at the money ceiling with its rifle in hand may spend
    all of it on a wither (`mc_bot_wither`). A bot does not shoot at a golem: one with a sword goes in,
    one without keeps away and says why, and a side that has met a golem buys swords the next round.
- **Minecraft weapons next to guns.** Minecraft armor reduces bullets only: swords, axes, arrows,
  explosions and fire go through it. Minecraft weapon damage is Minecraft's own table times
  `mc_weapon_scale` (1.5). With a Minecraft weapon in hand your aim is not kicked when you are hit; with a
  sword, an axe or the mace you also move 5% faster and bullets do not slow you.
- **Bots.** Guns stay a bot's first choice. A bot with a sword draws it when you are on top of it; against
  an opponent in Minecraft armor it uses its bow from afar and runs in with the sword from further out.
  Hurt and out of a fight, it eats its golden apple. A bot's kills pay it experience directly.
- **Bots with blocks and TNT.** Blocks and TNT are dear, and what is set down is gone with the round. A bot
  buys blocks (up to 8) only with its guns and armor paid for, more readily when blocks have been
  winning its side rounds, and keeps what it has not used; a rare rich one buys TNT with a flint and steel.
  Of what, it decides by a read of the other side: planks when few of them can field a rifle this round
  (in their hands, or the money for one), or when they came through the last walls with pickaxes, or when
  its own money reaches for a wall in wood only; else stone. The read is sometimes wrong. A side that ran
  into wooden walls buys axes the next round, one that ran into stone ones pickaxes.
  Nothing it does with them is on a cue. Each use is a bot weighing the moment it is in: what the moment is
  worth, its own nerve that round, its side's mood for that kind of thing that round, and a throw of the
  dice. The same situation does not always get the same answer, and a side builds a few things a round at
  most.
  - *Holding an open spot* for a while (4 to 13 s, each bot differently): cover where it stands (two pillars
    with a gap to shoot through, or a low block with a pillar on one flank), or two blocks stacked under its
    own feet where something close by can be looked over. It faces where it last saw you, else the way in
    you are expected to take, else the lane in front of it.
  - *A way into the site its side defends.* Shut, it sends you the long way round, or makes you dig in the
    open, or costs you TNT. Against that, the builder stands there with stone in its hands: it weighs how
    soon you can be at the gap against how long the wall takes and whether a teammate has the gap in sight
    (it asks one to cover). Shot at while it builds, it drops the job and fights. Afterwards the wall is a
    sixth player or a bluff: the builder stays behind it and listens (whoever digs has his hands full,
    whoever blows it walks into a rifle), or leaves it standing alone and goes to the other bomb site. You
    cannot tell which from outside. It says which to its own side ("Holding behind the wall", "Wall's up.
    Rotating").
  - *You near and out of sight, a gap between.* Hurt or outnumbered and falling back, it shuts the way
    behind it; defending, it shuts it when it hears you coming.
  - *At the bomb* with enemies about and time in hand, a Counter-Terrorist puts blocks up towards where the
    shots would come from, then defuses behind them.
  - *Shot at in the open* from far off: two blocks between itself and the shooter.
  - *TNT.* To the cover an enemy holds; through a wall of blocks in its way; or through a thin wall of a bomb
    site when the walk round to the doors is long, to come in where nobody watches (on de_dust2: the walls
    of the passage to B). It runs back along its own trail from the fuse, waits while a teammate is in the
    blast, and climbs out of its own crater on blocks. TNT somebody lit follows `mp_friendlyfire`, like a
    grenade.
  - *A mine.* A defender with TNT and a pressure plate, near a way into the site its side holds, with time
    before you can be there: the plate in the middle of the gap, the TNT on the floor one cell inside,
    past the frame, where it is seen from inside only. Whoever steps on the plate has four seconds
    (Minecraft's fuse): the one who runs on is unhurt, whoever is within a block and a half of the TNT
    when it goes dies, at three blocks it costs 90 of 100 health, and it leaves a crater in the way. Its
    own side keeps off it and is spared by the blast unless friendly fire is on. A bot of the other side
    has one look as it comes near: the more skilled it is the likelier it sees the plate, says so and
    goes round. With `mc_bot_mine_buried 1` the bot digs the floor cell under the plate out and sets the
    TNT in it (the one cell of the map a bot ever digs): nothing shows but the plate, but the hole
    smothers the blast (25 of 100 health at a block and a half).
  - *A wall in its way.* Around, if that is not much longer. Else one bot digs (about a second a block with
    a pickaxe, 7.5 s by hand) and the others cover it, while defenders who hear their wall being dug wait
    for the block to break. A side that ran into walls buys more pickaxes the next round. Bots only ever
    mine blocks somebody placed, never the map.
  - *Bad ground.* In a hole it cannot jump out of, a bot with blocks sets one under its own feet, as often
    as it takes. A trench across its way it bridges or climbs through, whichever takes fewer blocks.
- **What the bots say, and the round's story.** A bot tells its own side what it is doing with its blocks
  ("Wall going up", "Digging through. Cover me", "They're digging at the wall!", "Mine! Going around", "Wall's
  up. Rotating"), one line every few seconds at most. When a round is over everybody gets one line on how it
  was won and the two things most worth telling of it, what a bot kept to itself included: "Round to the
  Terrorists, the bomb went off. Witch put blocks up at the bomb before defusing. Ravager walled the B doors
  with stone and waited behind it." On de_dust2 places have their names; elsewhere it is the nearer bomb
  site.
- **What the bots learn.** Three things, per map, kept in `cstrike/mc_brain_<map>.txt` (delete the file and
  they have forgotten). What you do at a wall across your way: dig through it, have it blown open, or turn
  away from it (a wall still standing when you are 700 units off, or at the end of the round). Against a
  player who comes through walls a builder stays behind its wall more often (up to 85% of the time),
  against one who turns away it leaves more often (down to 25%); and the more often walls were blown open,
  the further back it waits. The way into a bomb site you take, as a Terrorist going in and as a
  Counter-Terrorist coming back to a planted bomb: counts in which recent rounds weigh more, also by the way
  you took the round before; their cover and walls face where you are expected. And which kinds of chance
  are worth taking against you: for each side and kind of moment (a held spot, a gap, a defuse, under fire,
  TNT in the pack), Thompson sampling between taking such chances this round and leaving them, paid with
  the round's result over about the last twenty rounds it came up in. That is the side's mood for the round,
  and it also sets how keen its bots are to buy blocks. `mc_brain` in the console prints all of it.
  `mc_bot_learn 0` keeps to even chances and the shortest way in.
- **Enchanting.** Right-click an enchanting table for Minecraft's screen: click an item in your inventory
  to put it on the table, then one of the three offers. An offer asks for a level and costs 1, 2 or 3
  levels and as many lapis lazuli; bookshelves two blocks from the table with air between raise the levels
  (15 of them give a level-30 third offer). One enchantment of the offer is shown beforehand; the rest is
  a surprise, drawn the way Minecraft draws it. Sharpness, Power, Protection, Efficiency, Unbreaking, Fire
  Aspect, Flame, Knockback and Punch are in, with their effects (Protection takes 4% a level off any
  damage, blades included). The table ($400), bookshelves ($200) and lapis lazuli ($5) are in the buy menu
  under Tools & Utilities. A bot with levels to spend buys a table, sets it down and enchants its gear.
  Not in yet: the item glint, and crossbow enchantments.
- **Items.** Golden apple, cooked beef, totem of undying (cheats a lethal hit), ender pearl (teleport plus 5
  damage), Bottle o' Enchanting, flint and steel, plus 62 placeable blocks and the ores' drops.
- **Minecraft HUD.** Hotbar (CS weapon icons in slots 1-5), hearts, armor bar, XP bar and level, item name
  pop-ups, the attack indicator, chat toasts and the death screen. The first-person hand is a port of
  `ItemInHandRenderer`, including equip and swing animations.
- **Minecraft sounds.** 102 sound events and 311 variants from your own Minecraft install. CS footsteps are
  replaced by the step sound of the block you stand on.
- **Map changes.** You can switch maps (de_dust2 ↔ mc_dust2) and the Minecraft inventory comes with you.

### Settings (console)

| Cvar | Default | Meaning |
|---|---|---|
| `mc_gui` | 1 | B opens the inventory screen (0: the old text menu) |
| `mc_buy_inventory` | 1 | 0 gives B back to the CS buy menu |
| `mc_players` | 1 | Minecraft player models (0: CS models, 2: both, for debugging) |
| `mc_world_reset` | 1 | Restore mc_dust2 at every round start |
| `mc_bot_armor` | 1 | Bots sometimes spawn in Minecraft armor |
| `mc_xp_per_kill` | 12 | XP dropped by a killed player (more if they had levels) |
| `mc_autokit` | 0 | Give the full diamond kit on first spawn |
| `mc_economy` | 1 | Minecraft items cost Counter-Strike money (0: free) |
| `mc_buy_anywhere` | 0 | 1: Minecraft items can be bought outside buy zones and after buy time |
| `mc_weapon_scale` | 1.5 | Damage of Minecraft weapons, as a multiple of Minecraft's own |
| `mc_bot_mcgear` | 45 | Percent of rounds in which a bot shops for Minecraft gear |
| `mc_bot_tactics` | 1 | Bots use blocks and TNT (0: they neither buy nor use them, nor dig) |
| `mc_bot_builders` | 35 | Percent of bots that think of buying blocks in a round (what has been winning shifts it) |
| `mc_bot_blocks` | 8 | The most stone blocks a bot buys |
| `mc_bot_pocket` | 0 | Blocks every bot is given free, to get over craters (0: only those who bought some can) |
| `mc_bot_tnt` | 8 | Percent of bots that buy TNT when they can afford it on top of a rifle |
| `mc_bot_learn` | 1 | Bots learn your way in, what you do at a wall and which chances are worth taking (0: even chances) |
| `mc_golem_max` | 1 | Living iron golems a side may have at once |
| `mc_wither_rounds` | 8 | Rounds a side waits between two withers |
| `mc_bot_golem` | 40 | Percent of bots with the money for it that buy an iron golem's parts when their side has none |
| `mc_bot_wither` | 50 | Percent of bots at the money ceiling, rifle in hand, that buy a wither's parts when their side may raise one |
| `mc_bot_mine_buried` | 0 | 1: a bot's mine has its TNT buried under the plate (hidden, but a far weaker blast) |
| `mc_obsidian_max` | 2 | The most obsidian a player can buy up to (0: no limit) |
| `mc_spawn_guard` | 320 | No blocks this near the other side's spawn points, in units (0: anywhere) |
| `mc_spawn_guard_time` | 25 | ... for so many seconds from the start of a round |
| `mc_fire_speed` | 1 | How fast fire burns and spreads (1: Minecraft's pace, 3: three times as fast) |
| `mc_hitbox_show` | 0 | 1 outlines the hit boxes of the Minecraft model you aim at |

Bot count and other settings are in `game/Half-Life/cstrike/csmc.cfg`. Add more creepers with `bot_add Creeper`.

## How it works

```
GoldSrc engine (HL25, build 10210, unmodified)
 ├─ cl_dlls/client.dll   our proxy  → loads the official CS client as cs_client.dll and wraps it
 │     code/client/src   classic-map and voxel renderers, Minecraft player models, hand, HUD,
 │                       inventory screens, particles, entities, sounds, music, input
 ├─ dlls/mp.dll          ReGameDLL_CS (open-source CS game DLL) + our Minecraft module
 │     code/server/mc    inventory, items, combat, XP, hunger, redstone, entities (orbs, items,
 │                       arrows, TNT...), digging/explosions, creepers, test scenarios
 └─ code/shared          compiled into both: BSP loader + hull traces, collision, block/item registries,
                         shared player movement (so prediction matches the server), sounds
```

- **Collision.** Every game-DLL trace and the player-movement traces (in both DLLs) go through wrappers that
  also hit the voxel world. The engine itself only sees a sealed sky-box "shell" BSP.
- **Networking.** Inventory, stats, effects, world data and block changes travel as user messages
  (`McInv`, `McStat`, `McFx`, `McVox`, `McBreak`, `McHello`, `McToast`). `delta.lst` is widened so player
  flags, armor codes and firework boosts reach the client prediction code.
- **Map conversion.** `tools/voxelizer` reads de_dust2's BSP and supersamples point contents into half-block
  cells. It keeps thin walls watertight, maps WAD textures to blocks, adds underground terrain and ores, and
  compiles the shell BSP with SDHLT. `tools/navgen` builds the bots' nav mesh from the voxels.
- **Assets.** `tools/assets` fetches the textures and sounds from Mojang's servers for your own Minecraft
  account, converts the sounds to WAV and generates the C++ sound table. They stay local and are not part of
  this repo.
- **Toolchain.** clang-cl and lld for 32-bit Windows, against the MSVC CRT and Windows SDK fetched by xwin
  (portable, no install). CMake and Ninja. `tools/build.sh` builds both DLLs and deploys them.

### Testing

- `tools/runtest.sh <map> <scenario>` starts the game with a scripted scenario (`mc_testscript`). The server
  drives the player and logs `SHOT name` lines, and the runner captures the game window at each one. It also
  sends real keys and mouse clicks when the scenario logs `KEYS ...`. Scenarios: `combat elytra mine tnt tour
  model items creeper creeperwatch enderman changelevel gui lab bomb hitbox fire light economy enchant shop
  tactics learn blasts materials walls trap golem wither mobsoak`. Scenarios run without the freeze and on a 9-minute round (the
  launchers set that); pass `mp_freezetime` and the rest in `TESTCMDS` to test on the play clocks.
- `tools/runtest_headless.sh <map> <scenario>` runs a scenario on the dedicated server (`hlds.exe`) instead:
  no window, nothing on the screen, and it runs beside a game somebody is playing. A bot stands in for the
  player. It is for what the log alone can check (bots, economy, world rules); `tactics`, `learn`,
  `materials` (real bullets of five guns at a bot behind each kind of block, and the rules about where
  blocks may go), `walls` (what the bots make of the player at a wall, and what they buy to build with) and
  `blasts` (a stress run: a blast under a random bot every second and a half, for as long as the timeout)
  are written for it, and the `tactics:` lines of the log give each bot's reasons. In a test run a watchdog
  writes to `logs/mc_watchdog.log` where the mod's code was when the server stopped finishing frames, and a
  crash handler what it died of (offsets into `mp.dll`, to look up in the build's `mp.map`).
- `tools/runtest_hidden.sh <map> <scenario>` runs the real game for what has to be looked at, on a Windows
  desktop of its own: no window on the screen, no focus taken, no mouse or keyboard touched. The game writes
  its own picture for every `SHOT` line (`mc_shot`), and the runner leaves them as PNGs in `$SHOTDIR`.
  Scenarios that need real key presses cannot run there.
- `tools/userflow.sh` plays like a person: the launcher's settings, then real keypresses and mouse input
  (MOTD → team menu → spawn → inventory → ...), with screenshots.

### Build

```bash
tools/build.sh      # configure + build client.dll and mp.dll, stop the game if running, deploy
tools/launch.ps1 -Map mc_dust2 -Bots 6
```

## Known issues and limits

- **Esc** closes the inventory but also opens the engine's pause menu. The engine handles Esc before the
  client DLL sees it. **B** or **E** closes the screen cleanly.
- Some rounds bots buy Minecraft gear (armor up to iron, bows, crossbows, swords; `mc_bot_mcgear` sets how
  often). Otherwise they fight with CS weapons. In the block world, CS guns held by Minecraft-skinned bots
  render as crossbows.
- A bot in the cover it built stays put (between two pillars it sees only through the gap, about 18 degrees
  to either side); it leaves when you are on top of it, when it is badly hurt, or after 30 to 55 s.
- TNT in this sand digs a crater two or three blocks deep. A bot without blocks that falls into one stays
  there (bots do not dig the map); `mc_bot_pocket 4` gives every bot blocks to climb out with.
- The world resets every round by default. Set `mc_world_reset 0` to keep changes until the map changes.
- Minecraft assets come from your own Minecraft account (`assets/`, git-ignored) and are not redistributable.
- An iron golem is hard on bots: they cannot shoot it, and those without a sword only keep away from it. In
  bots-only test matches a single golem has killed most of the other side. A side that met one buys swords
  the next round; whether that is enough has not been measured.
- A golem and a wither have a player's body for the map (they pass where a player passes) though they are
  drawn larger, so they overlap doorways and low ceilings. A wither does not fly over walls: it moves along
  the ground a bot would take, hanging in the air only where the ground has been blown away.
- Bots raise a wither rarely: it takes all the money a player can hold. It is mostly a player's weapon.
- No water or lava. Dust II has none of consequence.
- All testing so far is the author's own: in-game scenario runs, screenshots and logs. No independent
  review has taken place yet.
