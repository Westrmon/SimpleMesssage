@echo off
rem ===========================================================================
rem SimpleMessage — 撤销开机自启（Windows）
rem
rem 删除 install-autostart.bat 写入的那个值，不触碰 Run 键下的其它条目。
rem 键或值不存在时视为成功（幂等）。
rem ===========================================================================

setlocal EnableExtensions

set "APP_NAME=SimpleMessage"
set "RUN_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Run"

reg delete "%RUN_KEY%" /v "%APP_NAME%" /f >nul 2>&1

if errorlevel 1 (
    echo [提示] 注册表里没有找到 "%APP_NAME%" 自启项，无需撤销。
) else (
    echo [完成] 已撤销开机自启：%RUN_KEY%  "%APP_NAME%"
)

endlocal
