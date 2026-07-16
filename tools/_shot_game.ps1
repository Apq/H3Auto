Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win {
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$procs = Get-Process | Where-Object { $_.MainWindowTitle -match 'Heroes|HD|Might' -and $_.MainWindowHandle -ne 0 }
if (-not $procs) {
    Write-Host "NO GAME WINDOW; listing titled windows:"
    Get-Process | Where-Object { $_.MainWindowTitle -ne '' } | ForEach-Object { Write-Host ("  {0} | {1}" -f $_.ProcessName, $_.MainWindowTitle) }
    exit 0
}
$h = $procs[0].MainWindowHandle
[Win]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 400
$r = New-Object Win+RECT
[Win]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$ht = $r.Bottom - $r.Top
Write-Host ("GAME {0} rect {1},{2} {3}x{4}" -f $procs[0].ProcessName, $r.Left, $r.Top, $w, $ht)
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$out = "D:\GitHub\H3\H3Auto\_game_shot.png"
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host $out
