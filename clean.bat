@echo off
set PYTHONLEGACYWINDOWSSTDIO=1
set PYTHONIOENCODING=utf-8
"%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" run --target clean 2>&1

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
