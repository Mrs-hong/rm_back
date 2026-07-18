@echo off
rem ==========================================================================
rem lib/ssh.bat — plink/pscp 封装（hostkey 缓存 / 重试 / 超时 / 退出码）
rem
rem 前置环境变量（由 run.bat 设置）：
rem   PLINK_PATH        plink.exe 完整路径
rem   PSCP_PATH         pscp.exe 完整路径
rem   SSH_USER          SSH 用户名
rem   DEVICE_IP         设备 IP
rem   SSH_PASSWORD      SSH 登录密码（-pw 明文，产线可接受）
rem   SSH_PORT          SSH 端口
rem   ROOT_PASSWORD     root 密码（仅用于构建远程 sudo -S 命令）
rem   SSH_RETRY         重试次数（来自 [timing] ssh_retry）
rem   RETRY_BACKOFF_BASE 退避基数（秒）
rem   EXEC_TIMEOUT_SEC  长命令超时（秒）
rem   PS_DIR            ps/ 目录绝对路径（用于 stream_decode.ps1）
rem
rem 调用方式：
rem   call lib\ssh.bat init
rem       预缓存 host key + 验证凭据（首次运行提示 y/n 自动接纳）
rem   call lib\ssh.bat exec "remote command"
rem       短命令同步执行，stdout 透传；返回 plink 退出码
rem   call lib\ssh.bat exec_long "remote command"
rem       长命令通过 PS 流式 UTF-8 解码 + 全局超时；返回 plink 退出码（124=超时）
rem   call lib\ssh.bat upload "<local>" "<remote>"
rem       pscp 上传，支持重试
rem ==========================================================================
goto :%1 2>nul
echo [ERROR] lib\ssh.bat: 未知函数 '%~1' 1>&2
exit /b 99

rem ===================== 内部工具 =====================

rem 退避等待：参数 %~1=当前重试序号(1-based)
:retry_sleep
set /a "_idx=%~1"
set /a "_backoff=!RETRY_BACKOFF_BASE!"
set /a "_k=1"
:backoff_loop
if !_k! lss !_idx! (
    set /a "_backoff=!_backoff!*2"
    set /a "_k+=1"
    goto :backoff_loop
)
timeout /t !_backoff! /nobreak >nul
exit /b 0

rem ===================== 函数: init =====================
rem 预缓存主机密钥 + 验证凭据
rem 使用 echo y | plink 自动接纳首次 host key（写入 HKCU 注册表）
:init
setlocal EnableDelayedExpansion
echo [INFO] 预缓存主机密钥并验证 SSH 凭据...

rem 第一次：尝试 -batch（若 host key 已缓存则成功）
"!PLINK_PATH!" -batch -pw "!SSH_PASSWORD!" -P !SSH_PORT! !SSH_USER!@!DEVICE_IP! "exit 0" >nul 2>&1
if !errorlevel! equ 0 (
    endlocal
    exit /b 0
)

rem 第二次：用 echo y 接纳新 host key
echo y | "!PLINK_PATH!" -pw "!SSH_PASSWORD!" -P !SSH_PORT! !SSH_USER!@!DEVICE_IP! "exit 0" >nul 2>&1
if !errorlevel! equ 0 (
    endlocal
    exit /b 0
)

rem 仍失败：诊断
echo [ERROR] SSH 凭据验证失败
echo   目标: !SSH_USER!@!DEVICE_IP!:!SSH_PORT!
echo   可能原因：
echo     - SSH 密码错误（检查 config.ini [device] ssh_password=）
echo     - 设备未启动 SSH 服务
echo     - 网络中途断开
endlocal
exit /b 1

rem ===================== 函数: exec =====================
rem 参数: %2=远程命令（短命令，同步执行，stdout 透传）
rem 实现: 重试 SSH_RETRY 次，退避 2/4/8s
:exec
setlocal EnableDelayedExpansion
set "_cmd=%~2"
set "_attempt=0"
:exec_retry
set /a "_attempt+=1"
"!PLINK_PATH!" -batch -pw "!SSH_PASSWORD!" -P !SSH_PORT! !SSH_USER!@!DEVICE_IP! "!_cmd!"
set "_rc=!errorlevel!"
if !_rc! equ 0 (
    endlocal
    exit /b 0
)
if !_attempt! lss !SSH_RETRY! (
    echo [WARN] SSH 命令失败 (rc=!_rc!)，重试 (!_attempt!/!SSH_RETRY!)...
    call :retry_sleep !_attempt!
    goto :exec_retry
)
endlocal & exit /b %_rc%

rem ===================== 函数: exec_long =====================
rem 参数: %2=远程命令（长命令，流式 UTF-8 解码 + 全局超时）
rem 通过 ps\stream_decode.ps1 包装：RAW 字节流 + 状态机 UTF-8 解码 + WaitForExit 超时
rem 返回：plink 退出码；124=超时
:exec_long
setlocal EnableDelayedExpansion
set "PCBA_CMD=%~2"
set "EXEC_TIMEOUT_SEC=!EXEC_TIMEOUT_SEC!"
powershell -NoProfile -ExecutionPolicy Bypass -File "!PS_DIR!\stream_decode.ps1"
endlocal & exit /b %errorlevel%

rem ===================== 函数: upload =====================
rem 参数: %2=本地路径  %3=远程目标（user@host:path 或 user@host:dir/）
rem 支持 -r 透传：在本地路径前加 "-r " 表示递归
rem 实现: 重试 SCP_RETRY 次
:upload
setlocal EnableDelayedExpansion
set "_local=%~2"
set "_remote=%~3"
set "_recursive="
rem 检测 "-r " 前缀（递归上传目录）
set "_first=!_local:~0,3!"
if /i "!_first!"=="-r " (
    set "_recursive=-r"
    set "_local=!_local:~3!"
)
set "_attempt=0"
:upload_retry
set /a "_attempt+=1"
"!PSCP_PATH!" -batch -pw "!SSH_PASSWORD!" -P !SSH_PORT! !_recursive! "!_local!" "!SSH_USER!@!DEVICE_IP!:!_remote!"
set "_rc=!errorlevel!"
if !_rc! equ 0 (
    endlocal
    exit /b 0
)
if !_attempt! lss !SCP_RETRY! (
    echo [WARN] SCP 上传失败 (rc=!_rc!)，重试 (!_attempt!/!SCP_RETRY!)...
    call :retry_sleep !_attempt!
    goto :upload_retry
)
endlocal & exit /b %_rc%
