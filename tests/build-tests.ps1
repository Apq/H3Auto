$ErrorActionPreference = 'Stop'
$projDir = Split-Path -Parent $PSScriptRoot
Push-Location $projDir
try {
    & "C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe" tests\PolicyCoreTests.vcxproj /p:Configuration=Release /p:Platform=Win32 /m
    if ($LASTEXITCODE -ne 0) { throw "单元测试工程编译失败" }
    Write-Host "单元测试编译完成: $projDir\Release\tests\PolicyCoreTests.exe"
} finally {
    Pop-Location
}
