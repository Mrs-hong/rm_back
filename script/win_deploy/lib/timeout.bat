@echo off
rem ==========================================================================
rem lib/timeout.bat — 本地命令超时包装（PowerShell 实现）
rem
rem 用途：为"非 SSH 的本地命令"提供 wall-clock 超时保护。
rem       SSH 长命令请用 lib\ssh.bat exec_long（已内嵌 stream_decode.ps1 超时）。
rem
rem 调用方式：
rem   call lib\timeout.bat run_local <timeout_sec> <command...>
rem   超时则 taskkill 子进程并返回 124
rem ==========================================================================
goto :%1 2>nul
echo [ERROR] lib\timeout.bat: 未知函数 '%~1' 1>&2
exit /b 99

rem ===================== 函数: run_local =====================
rem 参数: %2=超时秒  %3...=命令及其参数
rem 实现：start /b 启动子进程，后台 timeout 计时，超时 taskkill
:run_local
setlocal EnableDelayedExpansion
set "_timeout=%~2"
set "_cmdline="
:build_cmd
shift
shift
set "_first=1"
:build_loop
if "%~1"=="" goto :run_it
if defined _first (
    set "_cmdline=%~1"
    set "_first="
) else (
    set "_cmdline=!_cmdline! %~1"
)
shift
goto :build_loop
:run_it
if "!_cmdline!"=="" (
    endlocal
    exit /b 1
)
rem 用 PowerShell 启动 + WaitForExit(timeout_ms) + Kill
powershell -NoProfile -Command ^
    "$p = Start-Process -FilePath cmd.exe -ArgumentList '/c','!_cmdline!' -PassThru -NoNewWindow -RedirectStandardOutput NUL; ^
     if (-not $p.WaitForExit(!_timeout!000)) { try { $p.Kill(); $p.WaitForExit(2000) } catch {}; exit 124 }; ^
     exit $p.ExitCode"
endlocal & exit /b %errorlevel%
