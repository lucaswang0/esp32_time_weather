@echo off
chcp 65001 >nul

echo ==============================================
echo     Weather Clock - Build Script
echo ==============================================
echo.
echo Building firmware for ESP32...
echo.

cd /d "%~dp0"
"%HOMEPATH%\.platformio\penv\Scripts\platformio.exe" run --target clean

echo.
echo ==============================================
if %errorlevel% equ 0 (
    echo Build successful!
) else (
    echo Build failed, error code: %errorlevel%
)
echo ==============================================
echo.
@REM pause