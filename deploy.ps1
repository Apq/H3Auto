$gameDir = 'D:\Heroes3\Heroes3_2026.05.01'
$packsDst = "$gameDir\_HD3_Data\Packs\战场自动化"
$src = "$PSScriptRoot\Release"

if (-not (Test-Path $packsDst)) {
    New-Item -ItemType Directory -Path $packsDst -Force | Out-Null
}
Copy-Item "$src\H3Auto.dll" $packsDst -Force
Copy-Item "$PSScriptRoot\H3Auto.ini" $packsDst -Force

$imgSrc = "$PSScriptRoot\img"
$imgDst = "$packsDst\img"
if (Test-Path $imgSrc) {
    New-Item -ItemType Directory -Path $imgDst -Force | Out-Null
    Get-ChildItem "$imgSrc\*.*" -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item $_.FullName $imgDst -Force
    }
}

Write-Host "已部署到 $packsDst"
