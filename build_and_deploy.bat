@echo off
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
if %errorlevel% neq 0 goto error

echo 开始部署...
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1"
if %errorlevel% neq 0 goto error

echo 编译并部署完成！
exit /b 0

:error
echo 编译或部署失败！
pause
exit /b 1