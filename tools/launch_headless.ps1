# Start the dedicated server (hlds.exe: no window, no rendering, nothing on the screen) from the test copy,
# for scenarios that are checked by the log alone. Its own port, so it runs beside a game somebody is
# playing. The PID is recorded so tools/stop_headless.ps1 stops only this.
param(
	[string]$Map = "de_dust2_mc",
	[int]$Bots = 8,
	[string]$Test = "",
	[int]$Port = 27017
)
$hl = "Z:\dev\CSminecraft\game_test\Half-Life"
$pidFile = "Z:\dev\scratch\csminecraft\hlds.pid"
# (scenarios are written for a round without a freeze and without the play clocks of csmc.cfg; -Test can set them back)
$cfg = "mp_freezetime 0`nmp_roundtime 9`nmp_buytime 9`nbot_quota $Bots`nbot_join_after_player 0`n" + ($Test -replace ';', "`n") + "`n"
Set-Content -Path "$hl\cstrike\csmc_test.cfg" -Value $cfg -Encoding ascii
# bots on a dedicated server have to be switched on before the game DLL starts (a listen server always has them)
$init = "$hl\cstrike\game_init.cfg"
if (Test-Path $init) {
	$t = Get-Content $init -Raw
	if ($t -match 'bot_enable\s+"0"') { Set-Content -Path $init -Value ($t -replace 'bot_enable\s+"0"', 'bot_enable "1"') -Encoding ascii -NoNewline }
}
# -condebug: the server's console goes to qconsole.log (what it said last when it stops by itself)
Remove-Item "$hl\qconsole.log" -ErrorAction SilentlyContinue
$args = "-console -condebug -game cstrike -insecure -nomaster -port $Port +sv_lan 1 +maxplayers 16 +map $Map"
$p = Start-Process -FilePath "$hl\hlds.exe" -ArgumentList $args -WorkingDirectory $hl -WindowStyle Hidden -PassThru
$p.Id | Out-File -Encoding ascii $pidFile
"started hlds.exe pid $($p.Id)"
