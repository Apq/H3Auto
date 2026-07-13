@echo off
setlocal

pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
if errorlevel 1 (
    pwsh -c "Write-Host 'Build failed.' -ForegroundColor Red"
    goto :error
)

echo Deploying...
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1"
if errorlevel 1 (
    pwsh -c "Write-Host 'Deploy failed.' -ForegroundColor Red"
    goto :error
)

pwsh -c "Write-Host 'Build and deployment completed.' -ForegroundColor Green"
exit /b 0

:error
echo.
pause
exit /b 1
