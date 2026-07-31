@echo off
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



