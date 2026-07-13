@echo off
chcp 936 >nul
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1"
if errorlevel 1 (
    pwsh -c "Write-Host '部署失败' -ForegroundColor Red"
    pause
    exit /b 1
)
pwsh -c "Write-Host '部署完成' -ForegroundColor Green"
