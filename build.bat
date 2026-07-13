@echo off
chcp 936 >nul
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe" H3Auto.vcxproj /p:Configuration=Release /p:Platform=Win32 /m
if errorlevel 1 (
    echo ±‡“Î ß∞‹
    pause
    exit /b 1
)
echo ±‡“ÎÕÍ≥…