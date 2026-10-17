@echo off
setlocal EnableExtensions

set "APP_NAME=SimpleMessage"
set "RUN_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Run"

reg delete "%RUN_KEY%" /v "%APP_NAME%" /f >nul 2>&1

if errorlevel 1 (
    echo [INFO] No autostart entry named "%APP_NAME%" found. Nothing to remove.
) else (
    echo [OK] Autostart removed: %RUN_KEY%  "%APP_NAME%"
)

endlocal