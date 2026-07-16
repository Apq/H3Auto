Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
# 抓取包含游戏面板的屏幕区域。游戏在副屏，面板内部原点(516,210)，
# 但显示可能被 HD mod 缩放。先抓整个虚拟屏幕，再交给模型看。
$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
Write-Host "VirtualScreen $($vs.X),$($vs.Y) $($vs.Width)x$($vs.Height)"
$bmp = New-Object System.Drawing.Bitmap($vs.Width, $vs.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($vs.X, $vs.Y, 0, 0, $bmp.Size)
$g.Dispose()
$out = "D:\GitHub\H3\H3Auto\_region_shot.png"
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host $out
