# MineStrike 1.6: the whole installation in one go. Double-click INSTALL.bat in the repository's folder.
#
# What it does, in this folder only (your Steam copy of Counter-Strike is read, never changed):
#   1. finds Counter-Strike 1.6 in your Steam library and copies its Half-Life folder to game\Half-Life
#   2. fetches the two small tools it needs: uv (which brings its own Python and the packages the scripts
#      use) and the SDHLT map compiler
#   3. fetches what the mod needs of Minecraft from Mojang's servers (textures, and the sounds it plays)
#   4. makes the block version of de_dust2 and its bot navigation from your own de_dust2
#   5. puts in the mod's two libraries (downloaded ready built from the project's releases) and its game files
#   6. has the bots work out de_dust2 on a server without a window, once (a few minutes)
# Run it again at any time: what is done already is skipped.
#
#   -HalfLife <folder>   your Half-Life folder, if it is not found by itself
#   -Bin <folder>        take client.dll and mp.dll from this folder (people who build them themselves)
param(
	[string]$HalfLife = "",
	[string]$Bin = "",
	[switch]$SkipNavigation
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$game = Join-Path $root 'game\Half-Life'
$cs = Join-Path $game 'cstrike'
$work = Join-Path $root 'work'
$release = 'https://github.com/zhadyz/minestrike-1.6/releases/latest/download/minestrike-bin.zip'
New-Item -ItemType Directory -Force $work | Out-Null

function Say([string]$text) { Write-Host ""; Write-Host "== $text" -ForegroundColor Cyan }
function Fail([string]$text) { Write-Host ""; Write-Host "STOPPED: $text" -ForegroundColor Red; exit 1 }
function Fetch([string]$url, [string]$to) {
	Write-Host "   downloading $url"
	Invoke-WebRequest -Uri $url -OutFile $to -UseBasicParsing
}

# ---- 1. Counter-Strike -------------------------------------------------------------------------------
Say "1 of 6: Counter-Strike 1.6"
if (-not (Test-Path (Join-Path $game 'hl.exe'))) {
	if (-not $HalfLife) {
		$steam = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
		$libs = @()
		if ($steam) {
			$libs += $steam
			$vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
			if (Test-Path $vdf) {
				foreach ($line in Get-Content $vdf) {
					if ($line -match '"path"\s+"(.+)"') { $libs += ($Matches[1] -replace '\\\\', '\') }
				}
			}
		}
		foreach ($lib in $libs) {
			$try = Join-Path $lib 'steamapps\common\Half-Life'
			if ((Test-Path (Join-Path $try 'hl.exe')) -and (Test-Path (Join-Path $try 'cstrike\cl_dlls\client.dll'))) { $HalfLife = $try; break }
		}
	}
	if (-not $HalfLife -or -not (Test-Path (Join-Path $HalfLife 'cstrike\cl_dlls\client.dll'))) {
		Fail "Counter-Strike 1.6 was not found. Install it in Steam and start it once, or run:  INSTALL.bat -HalfLife ""<your Half-Life folder>"""
	}
	Write-Host "   found at $HalfLife"
	Write-Host "   copying it to $game (your Steam copy stays as it is)"
	robocopy $HalfLife $game /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
	if ($LASTEXITCODE -ge 8) { Fail "copying Counter-Strike failed (robocopy code $LASTEXITCODE)" }
} else {
	Write-Host "   the game copy is there already"
}
Set-Content -Path (Join-Path $game 'steam_appid.txt') -Value '10' -Encoding ascii
# what the mod replaces is kept, and Counter-Strike's own client library gets the name the mod loads it by
$orig = Join-Path $cs '_csmc_originals'
foreach ($f in @('cl_dlls\client.dll', 'dlls\mp.dll', 'motd.txt')) {
	$keep = Join-Path $orig $f
	if (-not (Test-Path $keep) -and (Test-Path (Join-Path $cs $f))) {
		New-Item -ItemType Directory -Force (Split-Path $keep) | Out-Null
		Copy-Item (Join-Path $cs $f) $keep
	}
}
if (-not (Test-Path (Join-Path $cs 'cl_dlls\cs_client.dll'))) { Copy-Item (Join-Path $orig 'cl_dlls\client.dll') (Join-Path $cs 'cl_dlls\cs_client.dll') }

# ---- 2. tools ----------------------------------------------------------------------------------------
Say "2 of 6: tools (uv with its own Python, the SDHLT map compiler)"
$uv = Join-Path $root 'tools\uv\uv.exe'
if (-not (Test-Path $uv)) {
	$zip = Join-Path $work 'uv.zip'
	Fetch 'https://github.com/astral-sh/uv/releases/latest/download/uv-x86_64-pc-windows-msvc.zip' $zip
	Expand-Archive -Path $zip -DestinationPath (Join-Path $root 'tools\uv') -Force
}
$sdhlt = Join-Path $root 'tools\sdhlt\v130'
if (-not (Test-Path (Join-Path $sdhlt 'sdhlt-v1.3.0\tools\Win64\sdHLCSG_x64.exe'))) {
	$zip = Join-Path $work 'sdhlt_v130.zip'
	Fetch 'https://github.com/seedee/SDHLT/releases/download/v1.3.0/sdhlt_v130.zip' $zip
	Expand-Archive -Path $zip -DestinationPath $sdhlt -Force
}
# everything uv fetches stays under this folder
$env:UV_CACHE_DIR = Join-Path $work 'uv-cache'
$env:UV_PYTHON_INSTALL_DIR = Join-Path $work 'python'
$env:UV_NO_PROGRESS = '1'
$env:MINESTRIKE_GAME = $game
$env:MINESTRIKE_CSTRIKE = $cs
$env:MINESTRIKE_SCRATCH = $work
function Py([string[]]$with, [string[]]$rest) {
	$a = @('run', '--no-project', '--python', '3.12')
	foreach ($w in $with) { $a += @('--with', $w) }
	& $uv @a @rest
	if ($LASTEXITCODE -ne 0) { Fail "a step failed: $($rest -join ' ')" }
}

# ---- 3. Minecraft ------------------------------------------------------------------------------------
Say "3 of 6: Minecraft textures and sounds (from Mojang's servers, for the Minecraft you own)"
Py @('miniaudio', 'pillow', 'numpy', 'scipy') @((Join-Path $root 'tools\install\mc_setup.py'), '--cstrike', $cs, '--work', (Join-Path $work 'minecraft'))

# ---- 4. maps -----------------------------------------------------------------------------------------
Say "4 of 6: the block version of de_dust2 (about two minutes)"
if (-not (Test-Path (Join-Path $cs 'maps\mc_dust2.mcw')) -or -not (Test-Path (Join-Path $cs 'maps\mc_dust2.bsp'))) {
	Py @('numpy', 'scipy', 'pillow') @((Join-Path $root 'tools\voxelizer\build_world.py'))
} else { Write-Host "   there already" }
if (-not (Test-Path (Join-Path $cs 'maps\mc_dust2.nav'))) {
	Py @('numpy', 'pillow') @((Join-Path $root 'tools\navgen\navgen.py'), 'mc_dust2', '--maps-dir', (Join-Path $cs 'maps'), '--no-check')
}

# ---- 5. the mod --------------------------------------------------------------------------------------
Say "5 of 6: the mod's libraries and game files"
if (-not $Bin) {
	$zip = Join-Path $work 'minestrike-bin.zip'
	Fetch $release $zip
	$Bin = Join-Path $work 'bin'
	Expand-Archive -Path $zip -DestinationPath $Bin -Force
}
foreach ($f in @('client.dll', 'mp.dll')) { if (-not (Test-Path (Join-Path $Bin $f))) { Fail "$f is not in $Bin" } }
Copy-Item (Join-Path $Bin 'client.dll') (Join-Path $cs 'cl_dlls\client.dll') -Force
Copy-Item (Join-Path $Bin 'mp.dll') (Join-Path $cs 'dlls\mp.dll') -Force
Copy-Item (Join-Path $root 'gamedata\cstrike\*') $cs -Recurse -Force

# ---- 6. bot navigation -------------------------------------------------------------------------------
Say "6 of 6: bot navigation for de_dust2"
$nav = Join-Path $cs 'maps\de_dust2.nav'
if (-not (Test-Path $nav) -and -not $SkipNavigation) {
	Write-Host "   Counter-Strike has no bots of its own: they work the map out once, on a server without a"
	Write-Host "   window. This takes a few minutes; nothing shows on the screen meanwhile."
	Set-Content -Path (Join-Path $cs 'csmc_test.cfg') -Value "bot_quota 2`nmp_freezetime 0`n" -Encoding ascii
	$hlds = Start-Process -FilePath (Join-Path $game 'hlds.exe') -WorkingDirectory $game -WindowStyle Hidden -PassThru `
		-ArgumentList '-console -game cstrike -insecure -nomaster -port 27019 +sv_lan 1 +maxplayers 8 +map de_dust2'
	$deadline = (Get-Date).AddMinutes(20)
	$size = -1
	while ((Get-Date) -lt $deadline -and -not $hlds.HasExited) {
		Start-Sleep -Seconds 3
		if (Test-Path $nav) {
			# (written in one piece at the end: wait until it has stopped growing)
			$now = (Get-Item $nav).Length
			if ($now -gt 0 -and $now -eq $size) { break }
			$size = $now
		}
	}
	if (-not $hlds.HasExited) { Stop-Process -Id $hlds.Id -Force }
	Set-Content -Path (Join-Path $cs 'csmc_test.cfg') -Value '' -Encoding ascii
	if (-not (Test-Path $nav)) { Write-Host "   it did not finish. The bots will do it the first time you play de_dust2; then run INSTALL.bat again." -ForegroundColor Yellow }
}
Py @() @((Join-Path $root 'tools\setup\gen_gamedata.py'), $cs)

Say "Done. Start PLAY_MineStrike_dust2.bat"
Write-Host "   (This copy of the game is for playing offline with bots: do not connect it to online servers.)"
