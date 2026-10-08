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
| **B** | Creative inventory. Click an item to take a stack (right click takes one). Hover an item and press **6-9** to put it in that slot. The **Kits** tab equips whole sets. Tooltips show Minecraft's attribute lines, such as "When in Main Hand: 7 Attack Damage". The button at the bottom opens Counter-Strike's own buy menu. **B**, **E** or **Esc** closes it. |
| **1-5** | Your Counter-Strike weapons (they are slots 1-5 of the Minecraft hotbar) |
| **6-9** / mouse wheel | Minecraft hotbar slots |
| **Left click** | Attack (Minecraft cooldown, shown under the crosshair; crits while falling; sweeps). Hold it to mine a block. |
| **Right click** | Use: place blocks, open and close doors, equip armor, fire rockets, eat, throw pearls and XP bottles, draw the bow, light TNT with flint and steel |
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
- **Items.** Golden apple, cooked beef, totem of undying (cheats a lethal hit), ender pearl (teleport plus 5
  damage), Bottle o' Enchanting, flint and steel, plus 52 placeable blocks and the ores' drops.
- **Minecraft HUD.** Hotbar (CS weapon icons in slots 1-5), hearts, armor bar, XP bar and level, item name
  pop-ups, the attack indicator, chat toasts and the death screen. The first-person hand is a port of
  `ItemInHandRenderer`, including equip and swing animations.
- **Minecraft sounds.** 81 sound events and 276 variants from your own Minecraft install. CS footsteps are
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
  model items creeper creeperwatch enderman changelevel gui`.
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
- The world resets every round by default. Set `mc_world_reset 0` to keep changes until the map changes.
- Minecraft assets come from your own Minecraft account (`assets/`, git-ignored) and are not redistributable.
- No water or lava. Dust II has none of consequence.
- All testing so far is the author's own: in-game scenario runs, screenshots and logs. No independent
  review has taken place yet.
