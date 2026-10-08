# Send key presses to the foreground window using hardware scan codes (SDL2 games such as the
# HL25 GoldSrc engine ignore virtual-key-only synthetic input).
# usage: sendkeys.ps1 "b" "6" "1"      special names: enter esc tab space tilde up down left right
#        mouse: click rclick hold:<ms> rhold:<ms> move:<dx>:<dy> (relative motion) clickat:<x>:<y> (game client px) wait:<ms>
#        keys held: kdown:<key> ... kup:<key>
[CmdletBinding(PositionalBinding = $false)]
param([Parameter(Position = 0, ValueFromRemainingArguments = $true)][string[]]$Keys, [int]$DelayMs = 120)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class SK {
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public KEYBDINPUT ki; [FieldOffset(0)] public MOUSEINPUT mi; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U u; }
  [DllImport("user32.dll", SetLastError=true)] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  public struct POINT { public int X, Y; }
  public static void Mouse(int dx, int dy, uint flags) {
    INPUT[] i = new INPUT[1]; i[0].type = 0; i[0].u.mi.dx = dx; i[0].u.mi.dy = dy; i[0].u.mi.dwFlags = flags;
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Key(ushort scan, bool ext, bool up) {
    INPUT[] i = new INPUT[1]; i[0].type = 1; i[0].u.ki.wScan = scan;
    i[0].u.ki.dwFlags = 0x0008u | (up ? 0x0002u : 0u) | (ext ? 0x0001u : 0u);
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
$map = @{
  'esc'=0x01; '1'=0x02; '2'=0x03; '3'=0x04; '4'=0x05; '5'=0x06; '6'=0x07; '7'=0x08; '8'=0x09; '9'=0x0A; '0'=0x0B;
  'q'=0x10; 'w'=0x11; 'e'=0x12; 'r'=0x13; 't'=0x14; 'y'=0x15; 'u'=0x16; 'i'=0x17; 'o'=0x18; 'p'=0x19; 'enter'=0x1C;
  'a'=0x1E; 's'=0x1F; 'd'=0x20; 'f'=0x21; 'g'=0x22; 'h'=0x23; 'j'=0x24; 'k'=0x25; 'l'=0x26; 'tilde'=0x29;
  'z'=0x2C; 'x'=0x2D; 'c'=0x2E; 'v'=0x2F; 'b'=0x30; 'n'=0x31; 'm'=0x32; 'space'=0x39; 'tab'=0x0F; 'ctrl'=0x1D; 'shift'=0x2A;
}
$ext = @{ 'up'=0x48; 'down'=0x50; 'left'=0x4B; 'right'=0x4D }
# Input goes to whatever window has focus: only ever type into OUR test game, never into someone's
# other apps (and never steal focus to get there).
$gamePid = 0
try { $gamePid = [int](Get-Content "Z:\dev\scratch\csminecraft\hl.pid" -ErrorAction Stop) } catch {}
$fgPid = 0
[void][SK]::GetWindowThreadProcessId([SK]::GetForegroundWindow(), [ref]$fgPid)
if ($gamePid -eq 0 -or $fgPid -ne $gamePid) { Write-Output "sendkeys: skipped, the test game is not the foreground window"; exit 2 }
foreach ($k in $Keys) {
  $kk = $k.ToLower()
  if ($kk -eq 'click') { [SK]::Mouse(0, 0, 0x0002); Start-Sleep -Milliseconds 60; [SK]::Mouse(0, 0, 0x0004) }
  elseif ($kk -eq 'rclick') { [SK]::Mouse(0, 0, 0x0008); Start-Sleep -Milliseconds 60; [SK]::Mouse(0, 0, 0x0010) }
  elseif ($kk -like 'hold:*') { [SK]::Mouse(0, 0, 0x0002); Start-Sleep -Milliseconds ([int]$kk.Split(':')[1]); [SK]::Mouse(0, 0, 0x0004) }
  elseif ($kk -like 'rhold:*') { [SK]::Mouse(0, 0, 0x0008); Start-Sleep -Milliseconds ([int]$kk.Split(':')[1]); [SK]::Mouse(0, 0, 0x0010) }
  elseif ($kk -like 'move:*') {
    $parts = $kk.Split(':'); $dx = [int]$parts[1]; $dy = [int]$parts[2]
    # in small steps, like a real mouse (big jumps get clamped by raw-input smoothing)
    $n = [Math]::Max(1, [int]([Math]::Max([Math]::Abs($dx), [Math]::Abs($dy)) / 20))
    for ($s = 0; $s -lt $n; $s++) { [SK]::Mouse([int]($dx / $n), [int]($dy / $n), 0x0001); Start-Sleep -Milliseconds 8 }
  }
  elseif ($kk -like 'clickat:*') {
    # x:y in the game window's client area, as fractions of 1600x900 shot coordinates
    $parts = $kk.Split(':'); [SK]::SetProcessDPIAware() | Out-Null
    $gp = Get-Process -Id ([int](Get-Content "Z:\dev\scratch\csminecraft\hl.pid"))
    $pt = New-Object SK+POINT; $pt.X = [int]$parts[1]; $pt.Y = [int]$parts[2]
    [SK]::ClientToScreen($gp.MainWindowHandle, [ref]$pt) | Out-Null
    [SK]::SetCursorPos($pt.X, $pt.Y) | Out-Null; Start-Sleep -Milliseconds 80
    [SK]::Mouse(0, 0, 0x0002); Start-Sleep -Milliseconds 60; [SK]::Mouse(0, 0, 0x0004)
  }
  elseif ($kk -like 'kdown:*') { $n = $kk.Split(':')[1]; if ($map.ContainsKey($n)) { [SK]::Key([uint16]$map[$n], $false, $false) } }
  elseif ($kk -like 'kup:*') { $n = $kk.Split(':')[1]; if ($map.ContainsKey($n)) { [SK]::Key([uint16]$map[$n], $false, $true) } }
  elseif ($kk -like 'wait:*') { Start-Sleep -Milliseconds ([int]$kk.Split(':')[1]) }
  elseif ($ext.ContainsKey($kk)) { [SK]::Key([uint16]$ext[$kk], $true, $false); Start-Sleep -Milliseconds 40; [SK]::Key([uint16]$ext[$kk], $true, $true) }
  elseif ($map.ContainsKey($kk)) { [SK]::Key([uint16]$map[$kk], $false, $false); Start-Sleep -Milliseconds 40; [SK]::Key([uint16]$map[$kk], $false, $true) }
  else { Write-Output "unknown key $k" }
  Start-Sleep -Milliseconds $DelayMs
}
