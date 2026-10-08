@echo off
REM Counter-Strike 1.6: Minecraft Edition - the original de_dust2 with Minecraft items, armor, XP and elytra.
cd /d "%~dp0game\Half-Life"
type nul > cstrike\csmc_test.cfg
start "" hl.exe -game cstrike -insecure -novid +sv_lan 1 +maxplayers 16 +map de_dust2
