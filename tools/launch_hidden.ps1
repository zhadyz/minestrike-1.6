# Start the game on a Windows desktop of its own ("csmc_test"). Its window is not on the screen, cannot come
# to the front and gets no mouse or keyboard: nothing of it is seen or felt by whoever is at the computer.
# Pictures come from the game itself (mc_test_shots 1 -> cstrike\mc_shots\<name>.bmp). The PID is recorded
# like tools/launch.ps1 does, so tools/stop.ps1 stops it.
param(
	[string]$Map = "de_dust2_mc",
	[int]$Bots = 5,
	[string]$Test = "",
	[int]$W = 1280,
	[int]$H = 720
)
$hl = "Z:\dev\CSminecraft\game_test\Half-Life"
$pidFile = "Z:\dev\scratch\csminecraft\hl.pid"
# (scenarios are written for a round without a freeze and without the play clocks of csmc.cfg; -Test can set them back)
$cfg = "mp_freezetime 0`nmp_roundtime 9`nmp_buytime 9`nbot_quota $Bots`nmc_autojoin 1`nmotdfile none.txt`nmc_test_shots 1`n" + ($Test -replace ';', "`n") + "`n"
Set-Content -Path "$hl\cstrike\csmc_test.cfg" -Value $cfg -Encoding ascii
New-Item -ItemType Directory -Force "$hl\cstrike\mc_shots" | Out-Null
Remove-Item "$hl\cstrike\mc_shots\*.bmp" -ErrorAction SilentlyContinue
Remove-Item "$hl\qconsole.log" -ErrorAction SilentlyContinue
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class HD {
 [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
 public static extern IntPtr CreateDesktop(string name, IntPtr device, IntPtr devmode, int flags, uint access, IntPtr sa);
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
 public struct STARTUPINFO { public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
  public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
  public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError; }
 [StructLayout(LayoutKind.Sequential)]
 public struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
 [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
 public static extern bool CreateProcess(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
  IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
 // the process id, or minus the Windows error code
 public static int Run(string desktop, string cmdline, string cwd) {
  IntPtr d = CreateDesktop(desktop, IntPtr.Zero, IntPtr.Zero, 0, 0x10000000, IntPtr.Zero);
  if (d == IntPtr.Zero) return -Marshal.GetLastWin32Error();
  STARTUPINFO si = new STARTUPINFO(); si.cb = Marshal.SizeOf(typeof(STARTUPINFO)); si.lpDesktop = "WinSta0\\" + desktop;
  PROCESS_INFORMATION pi;
  if (!CreateProcess(null, new StringBuilder(cmdline), IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, cwd, ref si, out pi))
   return -Marshal.GetLastWin32Error();
  return pi.dwProcessId;   // (the game keeps its desktop alive from here on)
 }
}
"@
$cmd = "`"$hl\hl.exe`" -game cstrike -insecure -window -w $W -h $H -condebug -novid -nojoy -nomouse -noforcemparms +sv_lan 1 +maxplayers 16 +map $Map"
$id = [HD]::Run("csmc_test", $cmd, $hl)
if ($id -le 0) { "could not start hl.exe on its own desktop (Windows error $(-$id))"; exit 1 }
$id | Out-File -Encoding ascii $pidFile
"started hl.exe pid $id on desktop csmc_test"
