<!-- The sections below are pictures of docs/report/index.html (the full page with playable videos),
     rendered by tools/docs/render_readme.py so the README keeps its look on GitHub. -->

<a href="docs/report/media/"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/00-hero-dark.webp"><img alt="MineStrike 1.6. MineStrike 1.6 embeds Minecraft Java Edition gameplay in Counter-Strike 1.6 without modifying the engine binary. The Counter-Strike client library is closed-source. Its interface was recovered by static analysis of the shipped PE binaries and by instrumenting live calls, and it is ex" src="docs/readme/00-hero-light.webp" width="100%"></picture></a>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/01-facts-dark.png"><img alt="42 client exports forwarded by the proxy, 17 of them wrapped" src="docs/readme/01-facts-light.png" width="100%"></picture>

<a href="docs/report/media/"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/02-flight-dark.webp"><img alt="Figure 2. Elytra flight over the block version of de_dust2: T spawn, mid, CT spawn, A site and long A. Glide physics follow LivingEntity.travel; firework rockets boost the glide. The middle stretch is the third-person camera." src="docs/readme/02-flight-light.webp" width="100%"></picture></a>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/03-re-dark.png"><img alt="Reverse engineering of the shipped binaries. The engine (hw.dll, HL25 build 10210) and the official Counter-Strike client (client.dll) are distributed only as 32-bit PE binaries. No source code or symbol files for either were used. Analysis proceeded in two stages." src="docs/readme/03-re-light.png" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/04-server-dark.png"><img alt="Game logic on ReGameDLL_CS. The server library mp.dll is built from ReGameDLL_CS, an open-source reimplementation of Counter-Strike's game library, with a Minecraft module attached through its hook chains: spawn, damage, death, think, item use and buying. Minecraft's rules were reimplemented from th" src="docs/readme/04-server-light.png" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/05-classic-dark.jpg"><img alt="Collision and rendering of an excavatable de_dust2. In classic mode, the engine loads a sealed, empty sky-box map. The modification loads de_dust2.bsp (BSP version 30) itself: 9,582 planes, 2,766 nodes, 8,321 clipnodes, 5,383 faces and 10 solid brush models." src="docs/readme/05-classic-light.jpg" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/06-survival-dark.jpg"><img alt="Inventory, hunger and regeneration. The survival inventory (Shift+E) implements Minecraft's click semantics. Left-click picks up, places or swaps a stack. Right-click splits a stack or places a single item. Shift-click moves a stack to its preferred destination: armor to the armor slots, hotbar to s" src="docs/readme/06-survival-light.jpg" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/07-play-dark.png"><img alt="Launch configurations and controls. Three launchers in the repository root start a local, offline listen server with bots:" src="docs/readme/07-play-light.png" width="100%"></picture>

<a href="docs/report/media/"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/08-clips-dark.jpg"><img alt="Scripted scenarios. Each recording is a scripted scenario executed in the unmodified engine and captured from the game window. The server drives the local player. No frame was edited." src="docs/readme/08-clips-light.jpg" width="100%"></picture></a>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/09-stills-dark.jpg"><img alt="Models and interface. Figure 10. Elytra in third person, posed as in ElytraModel." src="docs/readme/09-stills-light.jpg" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/10-how-dark.png"><img alt="Components and toolchain. clang-cl and lld cross-compile 32-bit Windows libraries against the MSVC runtime and Windows SDK, obtained with xwin. No Visual Studio installation is required. CMake and Ninja drive the build." src="docs/readme/10-how-light.png" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/11-verification-dark.png"><img alt="Methodology and results. Collision was checked against the engine directly. With the engine running the real de_dust2, mc_bsptest fires randomized traces through both the engine's trace function and the modification's, and compares the end position, the start-solid flag, the surface normal and the p" src="docs/readme/11-verification-light.png" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/12-next-dark.png"><img alt="In progress. Minecraft music, played with Minecraft's track selection and silence intervals." src="docs/readme/12-next-light.png" width="100%"></picture>

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/readme/13-footer-dark.png"><img alt="" src="docs/readme/13-footer-light.png" width="100%"></picture>

---

## Getting it running

MineStrike 1.6 is a mod for your own copy of Counter-Strike 1.6. It ships no Valve or Mojang files: everything derived from them is created on your machine from your own copies.

1. **Counter-Strike 1.6** (Steam, HL25 build): copy your `Half-Life` folder to `game/Half-Life`, a private copy for the mod.
2. **Minecraft Java Edition**, owned by you: `tools/assets/fetch_mc_assets.py` and `tools/assets/fetch_music.py` download the textures, sounds and music from Mojang's servers for local use only.
3. **ReGameDLL_CS** source in `src/ReGameDLL_CS`; the server library is built from it.
4. **Toolchain**: clang-cl, lld, CMake and Ninja, with the MSVC runtime and Windows SDK from xwin. No Visual Studio is needed.
5. **Maps**: `tools/voxelizer` converts your `de_dust2.bsp` into the block world and the shell map. It uses [SDHLT v1.3.0](https://github.com/seedee/SDHLT/releases), unpacked into `tools/sdhlt/v130`.
6. **Game files**: `python tools/setup/gen_gamedata.py game/Half-Life/cstrike` makes the files based on Counter-Strike's own (the classic-mode map shell, its bot navigation and overview, `delta.lst`, `listenserver.cfg`).
7. `tools/build.sh` builds both libraries and deploys them with the files in `gamedata/`.

Then run `PLAY_MineStrike_dust2.bat`. Controls, settings and testing are in [docs/MANUAL.md](docs/MANUAL.md). The scripts still assume the checkout lives at `Z:\dev\CSminecraft`; making them location-independent is open work.

## Licence

GPL-3.0 (see [LICENSE](LICENSE)). The server library is built from [ReGameDLL_CS](https://github.com/rehlds/ReGameDLL_CS), which is GPL-3.0. Minecraft is a trademark of Mojang; Counter-Strike and Half-Life are trademarks of Valve. This project is not affiliated with either, and neither company's assets are included.

Built for the amigos.

\- --- / -- -.-- / -- --- --- -. / .- -. -.. / -- -.-- / ... - .- .-. ... --..-- / .... .- .--. .--. -.-- / -... .. .-. - .... -.. .- -.-- .-.-.-
