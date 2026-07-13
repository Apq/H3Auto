@echo off
setlocal

pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
if errorlevel 1 goto :error

echo Deploying...
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1"
if errorlevel 1 goto :error

echo Build and deployment completed.
exit /b 0

:error
echo.
echo Build or deployment failed. Press any key to close...
pause >nul
exit /b 1
