@echo off
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0pack.ps1" %*
if errorlevel 1 (
    echo 打包失败
    pause
    exit /b 1
)
pause
