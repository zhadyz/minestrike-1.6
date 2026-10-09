# Launch CS (Minecraft mod) from the Z: copy. Records the PID so tools/stop.ps1 only stops what we started.
# Game-DLL cvars can't go on the command line (the DLL loads with the map); mp.dll execs csmc.cfg and
# csmc_test.cfg at map start, so test settings are written to csmc_test.cfg here.
param(
	[string]$Map = "de_dust2",
	[int]$Bots = 5,
	[string]$Test = "",
	[string]$ClientArgs = "",
	[int]$W = 1600,
	[int]$H = 900,
	[switch]$User # exactly what the PLAY_*.bat launchers do (no test settings: MOTD, team menu, normal play)
)
# tests run in a separate copy so they never fight with a game someone is playing from game\
$hl = "Z:\dev\CSminecraft\game_test\Half-Life"
$pidFile = "Z:\dev\scratch\csminecraft\hl.pid"
# (scenarios are written for a round without a freeze and without the play clocks of csmc.cfg; -Test can set them back)
$cfg = "mp_freezetime 0`nmp_roundtime 9`nmp_buytime 9`nbot_quota $Bots`nmc_autojoin 1`nmotdfile none.txt`n" + ($Test -replace ';', "`n") + "`n"
if ($User) { $cfg = ($Test -replace ';', "`n") + "`n" }
Set-Content -Path "$hl\cstrike\csmc_test.cfg" -Value $cfg -Encoding ascii
$args = "-game cstrike -insecure -window -w $W -h $H -condebug -novid -nojoy -noforcemparms +sv_lan 1 +maxplayers 16 $ClientArgs +map $Map"
Remove-Item "$hl\qconsole.log" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "$hl\hl.exe" -ArgumentList $args -WorkingDirectory $hl -PassThru
$p.Id | Out-File -Encoding ascii $pidFile
"started hl.exe pid $($p.Id)"
