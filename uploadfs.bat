@echo off
cd /d "%~dp0"

"%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" run --target uploadfs

"%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" device monitor

echo.
echo ==============================================
if %errorlevel% equ 0 (
    echo Upload successful!
) else (
    echo Upload failed, error code: %errorlevel%
)
echo ==============================================
echo.
@REM pause