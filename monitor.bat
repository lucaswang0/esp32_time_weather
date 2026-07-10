@echo off
chcp 65001 >nul

echo ==============================================
echo     Weather Clock - Monitor Script
echo ==============================================
echo.
echo Starting serial monitor...
echo.

set "PIO_PATH=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"


cd /d "%~dp0"

"%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" device monitor

echo.
echo ==============================================
if %errorlevel% equ 0 (
    echo Monitor exited successfully!
) else (
    echo Monitor failed, error code: %errorlevel%
)
echo ==============================================
echo.
@REM pause