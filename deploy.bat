@echo off
chcp 936 >nul
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1"
if errorlevel 1 (
    echo ²¿ÊðÊ§°Ü
    pause
    exit /b 1
)
