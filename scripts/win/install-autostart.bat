@echo off
rem ===========================================================================
rem SimpleMessage — 注册开机自启（Windows）
rem
rem 【为什么是脚本而不是宿主内置功能】
rem 写注册表属于"系统级副作用"。由一个常驻后台程序在首次启动时静默地给自己
rem 加自启项，是很难被用户察觉、也很难被撤销的行为。做成显式脚本，
rem 用户看得见、能审查、能一键撤销。
rem
rem 用法：
rem   install-autostart.bat [可执行文件路径]
rem 不带参数时，默认使用本脚本上两级目录下的 sm_host.exe。
rem
rem 写入位置：HKCU\Software\Microsoft\Windows\CurrentVersion\Run
rem 【不需要管理员权限】——写的是当前用户，不是 HKLM。
rem ===========================================================================

setlocal EnableExtensions

set "APP_NAME=SimpleMessage"
set "RUN_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Run"

set "EXE_PATH=%~1"
if "%EXE_PATH%"=="" set "EXE_PATH=%~dp0..\..\sm_host.exe"

rem 把相对路径展开成绝对路径，注册表里【必须】是绝对路径 ——
rem 否则登录时的工作目录不确定，会导致找不到可执行文件。
for %%I in ("%EXE_PATH%") do set "EXE_PATH=%%~fI"

if not exist "%EXE_PATH%" (
    echo [错误] 找不到可执行文件：%EXE_PATH%
    echo        请把编译产物路径作为参数传进来，例如：
    echo          install-autostart.bat "D:\SimpleMessage\sm_host.exe"
    exit /b 1
)

rem /d 的值外面套一层引号，以应对路径里带空格的情况。
reg add "%RUN_KEY%" /v "%APP_NAME%" /t REG_SZ /d "\"%EXE_PATH%\"" /f >nul
if errorlevel 1 (
    echo [错误] 写入注册表失败。
    exit /b 1
)

echo [完成] 已注册开机自启：
echo        注册表键：%RUN_KEY%
echo        值名称：  %APP_NAME%
echo        可执行：  %EXE_PATH%
echo.
echo 撤销请运行：uninstall-autostart.bat

endlocal
