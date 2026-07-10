$gameDir = 'D:\Heroes3\Heroes3_2026.05.01'
$packsDst = "$gameDir\_HD3_Data\Packs\战斗自动"
$src = "$PSScriptRoot\Release"

# --- H3Auto 插件 ---
if (-not (Test-Path $packsDst)) {
    New-Item -ItemType Directory -Path $packsDst -Force | Out-Null
}
Copy-Item "$src\H3Auto.dll" $packsDst -Force
Copy-Item "$PSScriptRoot\H3Auto.ini" $packsDst -Force
Copy-Item "$PSScriptRoot\使用说明.txt" $packsDst -Force

Write-Host "已部署到 $packsDst"
