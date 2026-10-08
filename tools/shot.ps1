# Capture the CS game window (client area) to a downscaled PNG on Z:, even when other windows cover it
# (PrintWindow with PW_RENDERFULLCONTENT asks the window to render itself; no focus change).
param([string]$Out = "Z:\dev\scratch\csminecraft\shots\shot.png", [int]$Width = 960)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W { [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
 public struct RECT { public int L, T, R, B; } public struct POINT { public int X, Y; } }
"@
[W]::SetProcessDPIAware() | Out-Null
$pidFile = "Z:\dev\scratch\csminecraft\hl.pid"
$p = Get-Process -Id ([int](Get-Content $pidFile)) -ErrorAction Stop
$h = $p.MainWindowHandle
$wr = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$wr) | Out-Null
$cr = New-Object W+RECT; [W]::GetClientRect($h, [ref]$cr) | Out-Null
$pt = New-Object W+POINT; [W]::ClientToScreen($h, [ref]$pt) | Out-Null
$ww = $wr.R - $wr.L; $wh = $wr.B - $wr.T
$full = New-Object System.Drawing.Bitmap $ww, $wh
$g = [System.Drawing.Graphics]::FromImage($full)
$hdc = $g.GetHdc()
$ok = [W]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc); $g.Dispose()
$w = $cr.R - $cr.L; $hh = $cr.B - $cr.T
$ox = $pt.X - $wr.L; $oy = $pt.Y - $wr.T
$bmp = $full.Clone((New-Object System.Drawing.Rectangle $ox, $oy, $w, $hh), $full.PixelFormat)
$scale = $Width / $w
$out2 = New-Object System.Drawing.Bitmap ([int]($w*$scale)), ([int]($hh*$scale))
$g2 = [System.Drawing.Graphics]::FromImage($out2)
$g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g2.DrawImage($bmp, 0, 0, $out2.Width, $out2.Height)
New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
$out2.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"saved $Out ($w x $hh -> $($out2.Width) x $($out2.Height)) printwindow=$ok"
