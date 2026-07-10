@echo off

echo ==============================================
echo     Weather Clock - Monitor Script
echo ==============================================
echo.
echo Starting serial monitor...
echo.

set "PIO_PATH=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"

if not exist "%PIO_PATH%" (
    echo [ERROR] platformio.exe not found
    echo Expected: %PIO_PATH%
    echo Please install platformio for current user
    pause
    exit /b 1
)

cd /d "%~dp0"

"%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" run --target upload

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