@echo off
REM MineStrike 1.6: installs everything into this folder (see tools\install\install.ps1 for what it does).
REM You need Counter-Strike 1.6 installed in Steam, and you should own Minecraft: Java Edition.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install\install.ps1" %*
echo.
pause
