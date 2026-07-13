@echo off
chcp 936 >nul
setlocal

pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
if errorlevel 1 (
    pwsh -c "Write-Host '编译失败' -ForegroundColor Red"
    goto :error
)

echo 正在部署...
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1"
if errorlevel 1 (
    pwsh -c "Write-Host '部署失败' -ForegroundColor Red"
    goto :error
)

pwsh -c "Write-Host '编译并部署完成' -ForegroundColor Green"
exit /b 0

:error
echo.
pause
exit /b 1
