@echo off
setlocal EnableExtensions

set "APP_NAME=SimpleMessage"
set "RUN_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Run"

set "EXE_PATH=%~1"
if "%EXE_PATH%"=="" set "EXE_PATH=%~dp0.\sm_host.exe"
for %%I in ("%EXE_PATH%") do set "EXE_PATH=%%~fI"

if not exist "%EXE_PATH%" (
    echo [ERROR] Cannot find executable: %EXE_PATH%
    echo         Please pass the compiled exe path as an argument, for example:
    echo           install-autostart.bat "D:\SimpleMessage\sm_host.exe"
    exit /b 1
)

reg add "%RUN_KEY%" /v "%APP_NAME%" /t REG_SZ /d "\"%EXE_PATH%\"" /f >nul
if errorlevel 1 (
    echo [ERROR] Failed to write to registry.
    exit /b 1
)

echo [OK] Autostart registered.
echo      Registry key : %RUN_KEY%
echo      Value name   : %APP_NAME%
echo      Executable   : %EXE_PATH%
echo.
echo To undo, run: uninstall-autostart.bat

endlocal