# Stop the hl.exe we started (by recorded PID, verifying it runs from our Z: copy) and wait for it to exit.
$pidFile = "Z:\dev\scratch\csminecraft\hl.pid"
if (!(Test-Path $pidFile)) { "no pid file"; exit 0 }
$procId = [int](Get-Content $pidFile)
$p = Get-Process -Id $procId -ErrorAction SilentlyContinue
if ($p -and $p.Path -like "Z:\dev\CSminecraft\*") {
	Stop-Process -Id $procId -Force
	try { Wait-Process -Id $procId -Timeout 10 -ErrorAction Stop } catch {}
	"stopped $procId"
} else { "pid $procId not ours or not running" }
Remove-Item $pidFile -ErrorAction SilentlyContinue
