Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Text;
public class Win {
  [DllImport("user32.dll")] public static extern IntPtr GetDesktopWindow();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@ -ReferencedAssemblies System.Drawing

$found = @()
$cb = [Win+EnumProc]{
  param($h, $l)
  if ([Win]::IsWindowVisible($h)) {
    $len = [Win]::GetWindowTextLength($h)
    if ($len -gt 0) {
      $sb = New-Object System.Text.StringBuilder ($len + 1)
      [void][Win]::GetWindowText($h, $sb, $sb.Capacity)
      $t = $sb.ToString()
      if ($t -match 'Heroes|HD_|无敌|Might|Magic' -or $t -match 'H3') {
        $script:found += [pscustomobject]@{ H = $h; T = $t }
      }
    }
  }
  return $true
}
[void][Win]::EnumWindows($cb, [IntPtr]::Zero)
$found | ForEach-Object { Write-Host ("HWND {0:X} = {1}" -f [int64]$_.H, $_.T) }

$target = $found | Select-Object -First 1
if (-not $target) { Write-Host "NO_HEROES_WINDOW"; exit 2 }

$r = New-Object Win+RECT
[void][Win]::GetWindowRect($target.H, [ref]$r)
$w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
Write-Host ("rect {0},{1} {2}x{3}" -f $r.Left, $r.Top, $w, $hh)
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][Win]::PrintWindow($target.H, $hdc, 2)
$g.ReleaseHdc($hdc)
$out = "D:\GitHub\H3\H3Auto\_hwnd_shot.png"
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host $out
