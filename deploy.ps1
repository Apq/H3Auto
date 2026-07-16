$ErrorActionPreference = 'Stop'
$gameDir = 'D:\Heroes3\Heroes3_2026.05.01'
$packsDst = "$gameDir\_HD3_Data\Packs\打铁助手"
$legacyDst = "$gameDir\_HD3_Data\Packs\战场自动化"
$src = "$PSScriptRoot\Release"

try {
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

    # 旧包名会抢先加载同名 DLL；部署新包后移除旧 DLL，避免双加载。
    if (Test-Path "$legacyDst\H3Auto.dll") {
        Remove-Item "$legacyDst\H3Auto.dll" -Force
        Write-Host "已移除旧包 DLL: $legacyDst\H3Auto.dll"
    }

    Write-Host "已部署到 $packsDst"
} catch {
    Write-Host "部署错误: $_"
    exit 1
}