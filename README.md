<!-- The sections below are pictures of docs/report/index.html (the full page with playable videos),
     rendered by tools/docs/render_readme.py so the README keeps its look on GitHub. -->

<a href="docs/report/media/"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/00-hero-dark.webp"><img alt="MineStrike 1.6. MineStrike 1.6 embeds Minecraft Java Edition gameplay in Counter-Strike 1.6 without modifying the engine binary. The Counter-Strike client library is closed-source. Its interface was recovered by static analysis of the shipped PE binaries and by instrumenting live calls, and it is ex" src="docs/readme/00-hero-light.webp" width="100%"></picture></a>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/01-facts-dark.png"><img alt="42 client exports forwarded by the proxy, 17 of them wrapped" src="docs/readme/01-facts-light.png" width="100%"></picture>

<a href="docs/report/media/"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/02-flight-dark.webp"><img alt="Figure 2. Elytra flight over the block version of de_dust2: T spawn, mid, CT spawn, A site and long A. Glide physics follow LivingEntity.travel; firework rockets boost the glide. The middle stretch is the third-person camera." src="docs/readme/02-flight-light.webp" width="100%"></picture></a>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/03-re-dark.png"><img alt="Reverse engineering of the shipped binaries. The engine (hw.dll, HL25 build 10210) and the official Counter-Strike client (client.dll) are distributed only as 32-bit PE binaries. No source code or symbol files for either were used. Analysis proceeded in two stages." src="docs/readme/03-re-light.png" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/04-server-dark.png"><img alt="Game logic on ReGameDLL_CS. The server library mp.dll is built from ReGameDLL_CS, an open-source reimplementation of Counter-Strike's game library, with a Minecraft module attached through its hook chains: spawn, damage, death, think, item use and buying. Minecraft's rules were reimplemented from th" src="docs/readme/04-server-light.png" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/05-classic-dark.jpg"><img alt="Collision and rendering of an excavatable de_dust2. In classic mode, the engine loads a sealed, empty sky-box map. The modification loads de_dust2.bsp (BSP version 30) itself: 9,582 planes, 2,766 nodes, 8,321 clipnodes, 5,383 faces and 10 solid brush models." src="docs/readme/05-classic-light.jpg" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/06-survival-dark.jpg"><img alt="Inventory, hunger and regeneration. The survival inventory (Shift+E) implements Minecraft's click semantics. Left-click picks up, places or swaps a stack. Right-click splits a stack or places a single item. Shift-click moves a stack to its preferred destination: armor to the armor slots, hotbar to s" src="docs/readme/06-survival-light.jpg" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/07-combat-dark.jpg"><img alt="Hit boxes, fire and block light. Three places where Counter-Strike's rules disagreed with what the player sees, or with Minecraft's own rules, were resolved in favour of the latter." src="docs/readme/07-combat-light.jpg" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/08-play-dark.png"><img alt="Launch configurations and controls. Three launchers in the repository root start a local, offline listen server with bots:" src="docs/readme/08-play-light.png" width="100%"></picture>

<a href="docs/report/media/"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/09-clips-dark.jpg"><img alt="Scripted scenarios. Each recording is a scripted scenario executed in the unmodified engine and captured from the game window. The server drives the local player. No frame was edited." src="docs/readme/09-clips-light.jpg" width="100%"></picture></a>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/10-stills-dark.jpg"><img alt="Models and interface. Figure 13. Elytra in third person, posed as in ElytraModel." src="docs/readme/10-stills-light.jpg" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/11-how-dark.png"><img alt="Components and toolchain. clang-cl and lld cross-compile 32-bit Windows libraries against the MSVC runtime and Windows SDK, obtained with xwin. No Visual Studio installation is required. CMake and Ninja drive the build." src="docs/readme/11-how-light.png" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/12-verification-dark.png"><img alt="Methodology and results. Collision was checked against the engine directly. With the engine running the real de_dust2, mc_bsptest fires randomized traces through both the engine's trace function and the modification's, and compares the end position, the start-solid flag, the surface normal and the p" src="docs/readme/12-verification-light.png" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/13-next-dark.png"><img alt="In progress. Minecraft music, played with Minecraft's track selection and silence intervals." src="docs/readme/13-next-light.png" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/14-footer-dark.png"><img alt="" src="docs/readme/14-footer-light.png" width="100%"></picture>

---

## What is in it since the first release

- **Blocks are an economy.** A block costs by what it stops: wool, glass and sand ($50) stop no bullet, planks ($100) stop pistols but not rifles, stone ($200) stops every gun, obsidian ($800) stops TNT too. Rounds are short, so a wall buys seconds that matter.
- **Bots that build and breach.** They wall a way in and wait behind it or leave it as a bluff, dig through with cover, blow thin site walls open, lay mines, bridge craters. Nothing is on a cue: each use is a bot weighing its moment, and they learn the way in you take and what you do at a wall.
- **The iron golem.** Four iron blocks in a T and a carved pumpkin: it fights for your side and plays the objective. Bullets only slow it, and one in ten comes back at whoever fired it; only a sword hurts it.
- **The wither.** Four soul sand and three wither skeleton skulls, all the money a player can hold. It rises for eleven seconds, goes off with a blast the whole map hears, and hunts the other side with exploding skulls.
- **The round's story.** When a round ends everybody gets one line on how it was won and what was done with blocks, TNT and mobs.

Everything is described in [docs/MANUAL.md](docs/MANUAL.md).

## Getting it running

MineStrike 1.6 is a mod for your own copy of Counter-Strike 1.6. It ships no Valve or Mojang files: everything derived from them is made on your machine from your own copies.

**You need** Windows, **Counter-Strike 1.6 installed in Steam**, and you should own **Minecraft: Java Edition** (its textures and sounds are fetched from Mojang's servers for your own use).

1. Download this repository (the green **Code** button, then **Download ZIP**) and unpack it anywhere.
2. Double-click **`INSTALL.bat`**.
3. When it says *Done*, double-click **`PLAY_MineStrike_dust2.bat`**.

The installer works in that folder only and leaves your Steam game as it is. It copies your Half-Life folder there, fetches what it needs (Minecraft's textures and the sounds the mod plays, a Python it keeps to itself, the SDHLT map compiler, the mod's two libraries from this project's [releases](https://github.com/zhadyz/minestrike-1.6/releases)), makes the block version of de_dust2 from your own map, and has the bots work out de_dust2 once on a server without a window. Allow five to ten minutes and about 2 GB of disk. Run it again at any time: what is done already is skipped.

- If it does not find Counter-Strike: `INSTALL.bat -HalfLife "D:\SteamLibrary\steamapps\common\Half-Life"`.
- This copy of the game is for playing offline with bots (it runs in insecure LAN mode). Do not connect it to online servers.
- Controls and settings are in [docs/MANUAL.md](docs/MANUAL.md).

### Building the libraries yourself

Only if you want to change the code. The two libraries are 32-bit Windows DLLs built with clang-cl and lld (CMake and Ninja, the MSVC runtime and Windows SDK from xwin; no Visual Studio).

1. [ReGameDLL_CS](https://github.com/rehlds/ReGameDLL_CS) at commit `4a50c42e85fd3778c2b7d24731c7d30c83bbab01` into `src/ReGameDLL_CS`, then `python tools/build/patch_regamedll.py` (it inserts the mod's hooks, each marked `// [csmc]`).
2. The [Half-Life SDK](https://github.com/ValveSoftware/halflife) into `src/halflife`.
3. Configure `code/client` and `code/server` with `tools/clang-cl-x86.cmake` and build. `tools/build.sh` does this, but it still assumes the checkout lies at `Z:\dev\CSminecraft`.
4. `INSTALL.bat -Bin <folder with your client.dll and mp.dll>` installs them like the released ones.

## Licence

GPL-3.0 (see [LICENSE](LICENSE)). The server library is built from [ReGameDLL_CS](https://github.com/rehlds/ReGameDLL_CS), which is GPL-3.0. Minecraft is a trademark of Mojang; Counter-Strike and Half-Life are trademarks of Valve. This project is not affiliated with either, and neither company's assets are included.

Built for the amigos.

\- --- / -- -.-- / -- --- --- -. / .- -. -.. / -- -.-- / ... - .- .-. ... --..-- / .... .- .--. .--. -.-- / -... .. .-. - .... -.. .- -.-- .-.-.-
