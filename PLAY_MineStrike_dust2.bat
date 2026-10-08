@echo off
REM MineStrike 1.6 - the real de_dust2 with Minecraft built in: dig the map, place blocks, buy diamond
REM swords and crossbows, eat to regenerate. Bots on both teams.
REM Runs the private game copy in insecure LAN mode: never connect this copy to online servers.
cd /d "%~dp0game\Half-Life"
type nul > cstrike\csmc_test.cfg
start "" hl.exe -game cstrike -insecure -novid -fullscreen +sv_lan 1 +maxplayers 16 +map de_dust2_mc
