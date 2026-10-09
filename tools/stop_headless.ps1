# Stop an hlds.exe that tools/launch_headless.ps1 started, checking it runs from our Z: copy.
#   -ProcId N : that one process and no other (what a test runner passes: the server it started itself)
#   (nothing) : the one the PID file names (the last one started)
param([int]$ProcId = 0)
$pidFile = "Z:\dev\scratch\csminecraft\hlds.pid"
$fromFile = $ProcId -eq 0
if ($fromFile) {
	if (!(Test-Path $pidFile)) { "no pid file"; exit 0 }
	$ProcId = [int](Get-Content $pidFile)
}
$p = Get-Process -Id $ProcId -ErrorAction SilentlyContinue
if ($p -and $p.Path -like "Z:\dev\CSminecraft\*" -and $p.ProcessName -eq "hlds") {
	Stop-Process -Id $ProcId -Force
	try { Wait-Process -Id $ProcId -Timeout 10 -ErrorAction Stop } catch {}
	"stopped $ProcId"
} else { "pid $ProcId not ours or not running" }
# the PID file is only cleared by whoever stopped the process it names
if ((Test-Path $pidFile) -and ([int](Get-Content $pidFile) -eq $ProcId)) { Remove-Item $pidFile -ErrorAction SilentlyContinue }
