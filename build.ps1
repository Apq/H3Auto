$ErrorActionPreference = 'Stop'
$projDir = $PSScriptRoot
Push-Location $projDir
try {
    & "C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe" H3Auto.vcxproj /p:Configuration=Release /p:Platform=Win32 /m
    if ($LASTEXITCODE -ne 0) { Write-Host "Build failed."; exit 1 }
    Write-Host "Build output: $projDir\Release\H3Auto.dll"
} catch {
    Write-Host "Build error: $_"
    exit 1
} finally {
    Pop-Location
}
