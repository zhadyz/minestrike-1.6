@echo off
REM Counter-Strike 1.6: Minecraft Edition - Minecraft voxel de_dust2 with bots.
REM Runs the private game copy on Z: in insecure LAN mode: never connect this copy to online servers.
cd /d "%~dp0game\Half-Life"
type nul > cstrike\csmc_test.cfg
start "" hl.exe -game cstrike -insecure -novid +sv_lan 1 +maxplayers 16 +map mc_dust2
