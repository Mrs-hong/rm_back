@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion
rem Ensure working directory is the script directory (required for double-click
rem runs and for invocations from other directories: relative paths like
rem config\, scripts\, offline-debs\ and qifeng-scm-*.deb depend on it)
cd /d "%~dp0"
rem ==========================================================================
rem BM1684x Edge Device One-Click Self-Check Script (Windows)
rem
rem Function: Upload scripts/config/deb to /data/testcheck/, remotely invoke pcba_check.sh
rem           Unified hardware self-check + SCM install + SCM self-check, real-time output,
rem           all output saved to check_result.txt in current directory.
rem
rem Dependencies: Windows built-in OpenSSH or PuTTY (plink.exe / pscp.exe)
rem Recommended: Put plink.exe and pscp.exe in the same directory, supports -pw direct password
rem
rem Usage:
rem   run_check.bat                                  Use defaults
rem   run_check.bat 192.168.112.47 linaro            Custom IP and username
rem   run_check.bat --password mypwd --root-pwd rpwd Manually set passwords
rem   run_check.bat --vendor huayu                   Specify device vendor
rem   run_check.bat -h                               Show help
rem ==========================================================================

rem ===================== Default Parameters =====================
set "DEVICE_IP=192.168.112.223"
set "SSH_USER=linaro"
set "VENDOR=huayu"
set "CUSTOM_DEB="

rem Parse command line arguments
:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="-h" goto :show_help
if /i "%~1"=="--help" goto :show_help
if /i "%~1"=="--vendor" goto :parse_vendor
if /i "%~1"=="--deb" goto :parse_deb
if /i "%~1"=="--password" goto :parse_password
if /i "%~1"=="--root-pwd" goto :parse_root_pwd
if not defined DEVICE_IP_SET (
    set "DEVICE_IP=%~1"
    set "DEVICE_IP_SET=1"
    shift
    goto :parse_args
)
if not defined SSH_USER_SET (
    set "SSH_USER=%~1"
    set "SSH_USER_SET=1"
    shift
    goto :parse_args
)
shift
goto :parse_args

:parse_vendor
shift
if "%~1"=="" (
    echo [ERROR] --vendor requires a vendor name
    exit /b 1
)
set "VENDOR=%~1"
shift
goto :parse_args

:parse_deb
shift
if "%~1"=="" (
    echo [ERROR] --deb requires a file path
    exit /b 1
)
set "CUSTOM_DEB=%~1"
shift
goto :parse_args

:parse_password
shift
if "%~1"=="" (
    echo [ERROR] --password requires a password value
    exit /b 1
)
set "SSH_PASSWORD=%~1"
shift
goto :parse_args

:parse_root_pwd
shift
if "%~1"=="" (
    echo [ERROR] --root-pwd requires a root password value
    exit /b 1
)
set "ROOT_PASSWORD=%~1"
shift
goto :parse_args

:args_done

call :log [INFO] 连接 !SSH_USER!@!DEVICE_IP! 厂商:!VENDOR!

rem Fallback: read positional params directly if parser didn't set them
rem This handles the case where the Windows script version has different parsing code.
if not defined DEVICE_IP_SET (
    if not "%1"=="" (
        set "DEVICE_IP=%1"
        set "DEVICE_IP_SET=1"
    )
)
if not defined SSH_USER_SET (
    if not "%2"=="" (
        set "SSH_USER=%2"
        set "SSH_USER_SET=1"
    )
)

rem Passwords can be set via --password/--root-pwd or env vars; fallback to defaults
if not defined SSH_PASSWORD set "SSH_PASSWORD=linaro"
if not defined ROOT_PASSWORD set "ROOT_PASSWORD=linaro"

rem ===================== Remote Path Config =====================
set "REMOTE_TESTCHECK_DIR=/data/testcheck"
set "SSH_PORT=22"
set "SSH_TARGET=%SSH_USER%@%DEVICE_IP%"
set "RESULT_FILE=check_result.txt"

rem ===================== Detect SSH Tool =====================
rem Priority: 1) plink/pscp in script dir  2) plink/pscp in PATH  3) Windows OpenSSH
set "USE_PLINK=0"
set "SCRIPT_DIR=%~dp0"

rem 1. Check script directory
if exist "%SCRIPT_DIR%plink.exe" (
    if exist "%SCRIPT_DIR%pscp.exe" (
        set "USE_PLINK=1"
        set "PLINK_PATH=%SCRIPT_DIR%plink.exe"
        set SSH_CMD="%SCRIPT_DIR%plink.exe" -batch -pw !SSH_PASSWORD! -P !SSH_PORT! !SSH_TARGET!
        set SCP_CMD="%SCRIPT_DIR%pscp.exe" -batch -pw !SSH_PASSWORD! -P !SSH_PORT!
        call :log [INFO] 使用 plink/pscp 工具
        call :log [INFO] 预连接: !SSH_TARGET!
        rem Pre-cache host key and verify credentials to avoid -batch failures
        echo y | "%SCRIPT_DIR%plink.exe" -pw !SSH_PASSWORD! -P !SSH_PORT! !SSH_TARGET! "exit" >nul
        if errorlevel 1 (
            call :log [ERROR] SSH 连接失败，请检查 IP/用户名/密码
            call :PauseExit
            exit /b 1
        )
    )
)

rem 2. Check PATH
if "!USE_PLINK!"=="0" (
    where plink.exe >nul 2>&1
    if not errorlevel 1 (
        where pscp.exe >nul 2>&1
        if not errorlevel 1 (
            set "USE_PLINK=1"
            set "PLINK_PATH=plink.exe"
            set SSH_CMD=plink.exe -batch -pw !SSH_PASSWORD! -P !SSH_PORT! !SSH_TARGET!
            set SCP_CMD=pscp.exe -batch -pw !SSH_PASSWORD! -P !SSH_PORT!
            call :log [INFO] 检测到 PuTTY，使用 plink/pscp
            rem Pre-cache host key and verify credentials to avoid -batch failures
            echo y | plink.exe -pw !SSH_PASSWORD! -P !SSH_PORT! !SSH_TARGET! "exit" >nul
            if errorlevel 1 (
                call :log [ERROR] SSH 连接失败，请检查 IP/用户名/密码
                call :PauseExit
                exit /b 1
            )
        )
    )
)

rem 3. Fallback to Windows OpenSSH
if "!USE_PLINK!"=="0" (
    where ssh.exe >nul 2>&1
    if errorlevel 1 (
        call :log [ERROR] 未找到 ssh.exe 或 plink.exe，请安装 SSH 客户端
        call :PauseExit
        exit /b 1
    )
    set SSH_CMD=ssh.exe -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL -p !SSH_PORT! !SSH_TARGET!
    set SCP_CMD=scp.exe -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL -P !SSH_PORT!
    call :log [INFO] 使用 Windows OpenSSH（每次连接需输入密码）
)

rem ===================== Find deb Package =====================
if "%CUSTOM_DEB%"=="" (
    set "DEB_FILE="
    for /f "delims=" %%a in ('dir /b /o-d "qifeng-scm*.deb" 2^>nul') do (
        if not defined DEB_FILE set "DEB_FILE=%%a"
    )
    if not defined DEB_FILE (
        call :log [ERROR] 未找到 qifeng-scm*.deb 安装包
        call :PauseExit
        exit /b 1
    )
) else (
    set "DEB_FILE=%CUSTOM_DEB%"
)
call :log [INFO] 安装包: !DEB_FILE!

rem Find corresponding sha256 file
set "SHA256_FILE=!DEB_FILE!.sha256"
if not exist "!SHA256_FILE!" (
    set "SHA256_FILE="
    for /f "delims=" %%a in ('dir /b /o-d "qifeng-scm-*.deb.sha256" 2^>nul') do (
        if not defined SHA256_SET (
            set "SHA256_FILE=%%a"
            set "SHA256_SET=1"
        )
    )
)
if defined SHA256_FILE (
    call :log [INFO] 校验文件: !SHA256_FILE!
) else (
    call :log [WARN] 未找到校验文件，跳过完整性检查
    set "SHA256_FILE="
)

rem ===================== Initialize Output File =====================
rem 写入 UTF-8 BOM（0xEF 0xBB 0xBF），确保 Windows 记事本等编辑器正确识别中文编码
powershell -Command "[System.IO.File]::WriteAllBytes('!RESULT_FILE!', [byte[]](0xEF,0xBB,0xBF))" >nul 2>&1
echo ===== 设备自检开始 ===== >> "!RESULT_FILE!"
echo 设备: !SSH_USER!@!DEVICE_IP! >> "!RESULT_FILE!"
echo 时间: %date% %time% >> "!RESULT_FILE!"
echo. >> "!RESULT_FILE!"

rem ===================== Clean up old files (force overwrite) =====================
rem Remove: previously uploaded files, previous result/log files, and leftover
rem temp/pipe files on the device to avoid stale data affecting this run.
call :log [INFO] 清理远程目录...
!SSH_CMD! "echo !ROOT_PASSWORD! > /tmp/.sudo_pwd && sudo -S rm -rf !REMOTE_TESTCHECK_DIR! /data/check /tmp/pcba_check_output.log /tmp/scm_selftest_output.log /tmp/scm_install.log /tmp/eth_perf_detail.log /tmp/.sudo_pwd /tmp/fio_press_write_*.log /tmp/fio_press_read*.log < /tmp/.sudo_pwd"

rem ===================== Create Remote Directories =====================
call :log [INFO] 创建远程目录...
!SSH_CMD! "echo !ROOT_PASSWORD! > /tmp/.sudo_pwd && sudo -S mkdir -p !REMOTE_TESTCHECK_DIR!/config !REMOTE_TESTCHECK_DIR!/scripts/!VENDOR! < /tmp/.sudo_pwd && sudo -S chown -R !SSH_USER!:!SSH_USER! !REMOTE_TESTCHECK_DIR! < /tmp/.sudo_pwd && rm -f /tmp/.sudo_pwd"
if errorlevel 1 (
    call :log [ERROR] 创建远程目录失败
    call :CleanupRemote
    call :PauseExit
    exit /b 1
)

rem ===================== Upload Config =====================
call :log [INFO] 上传配置文件...
!SCP_CMD! "config\selftest.json" !SSH_TARGET!:!REMOTE_TESTCHECK_DIR!/config/selftest.json
if errorlevel 1 (
    call :log [ERROR] 配置文件上传失败
    call :CleanupRemote
    call :PauseExit
    exit /b 1
)

rem ===================== Upload Scripts =====================
call :log [INFO] 上传检测脚本...
!SCP_CMD! -r "scripts" !SSH_TARGET!:!REMOTE_TESTCHECK_DIR!/
if errorlevel 1 (
    call :log [ERROR] 检测脚本上传失败
    call :CleanupRemote
    call :PauseExit
    exit /b 1
)

rem ===================== Upload deb =====================
call :log [INFO] 上传安装包...
!SCP_CMD! "!DEB_FILE!" !SSH_TARGET!:!REMOTE_TESTCHECK_DIR!/
if errorlevel 1 (
    call :log [ERROR] 安装包上传失败
    call :CleanupRemote
    call :PauseExit
    exit /b 1
)

rem ===================== Upload sha256 =====================
if defined SHA256_FILE (
    call :log [INFO] 上传校验文件...
    !SCP_CMD! "!SHA256_FILE!" !SSH_TARGET!:!REMOTE_TESTCHECK_DIR!/
    if errorlevel 1 (
        call :log [ERROR] 校验文件上传失败
        call :CleanupRemote
        call :PauseExit
        exit /b 1
    )
)

rem ===================== Set Execute Permissions =====================
call :log [INFO] 设置脚本执行权限...
!SSH_CMD! "chmod +x !REMOTE_TESTCHECK_DIR!/scripts/*.sh !REMOTE_TESTCHECK_DIR!/scripts/!VENDOR!/*.sh" >nul 2>&1

rem ===================== Pre-check deps, upload offline-debs if needed =====================
rem Run check_scm_deps.sh remotely first; only when dependencies are missing
rem upload the (possibly large) offline-debs/ directory. pcba_check.sh phase 4
rem will then run offline-debs/install_offline.sh to install them.
if exist "offline-debs\install_offline.sh" (
    call :log [INFO] 检测 SCM 依赖...
    !SSH_CMD! "bash !REMOTE_TESTCHECK_DIR!/scripts/check_scm_deps.sh" >nul 2>&1
    if errorlevel 1 (
        call :log [INFO] 缺少依赖，上传离线包...
        !SCP_CMD! -r "offline-debs" !SSH_TARGET!:!REMOTE_TESTCHECK_DIR!/
        if errorlevel 1 (
            call :log [ERROR] 离线包上传失败
            call :CleanupRemote
            call :PauseExit
            exit /b 1
        )
    ) else (
        call :log [INFO] 所有依赖已安装，跳过离线包上传
    )
)

call :log [INFO] 文件上传完成
echo. >> "!RESULT_FILE!"

rem ===================== Execute pcba_check.sh (real-time UTF-8 console output) =====================
call :log [INFO] 开始设备自检...
echo [INFO] Starting pcba_check.sh... >> "!RESULT_FILE!"
echo ============================================================ >> "!RESULT_FILE!"

rem Encoding design (UTF-8 end to end, no conversion):
rem   pcba_check.sh outputs UTF-8 on the device. Force the console codepage to
rem   UTF-8 (65001) right before execution so plink's raw UTF-8 bytes and the
rem   downloaded log displayed via `type` both render Chinese correctly.
rem   Works in both cmd.exe and PowerShell terminals (codepage is per-console).
chcp 65001 >nul
call :InitPcbaCmd

call :RunPcbaCheck

rem Download the remote log file and append to local result file
rem (already displayed in real-time via run_remote.ps1; only download, no re-display)
call :log [INFO] 下载完整输出...
!SCP_CMD! !SSH_TARGET!:!REMOTE_LOG! "!RESULT_FILE!.tmp" >nul 2>&1
if exist "!RESULT_FILE!.tmp" (
    copy /b "!RESULT_FILE!"+"!RESULT_FILE!.tmp" "!RESULT_FILE!" >nul
    del "!RESULT_FILE!.tmp" >nul
)

rem Parse the real pcba_check.sh exit code from the downloaded log (PCBA_EXIT=N).
rem The console pipe goes through PowerShell, so plink's errorlevel is unavailable.
set "CHECK_RESULT=1"
for /f "tokens=2 delims==" %%a in ('findstr /b "PCBA_EXIT=" "!RESULT_FILE!"') do set "CHECK_RESULT=%%a"
rem Strip trailing whitespace/CR if any
for /f "tokens=1" %%a in ("!CHECK_RESULT!") do set "CHECK_RESULT=%%a"

echo. >> "!RESULT_FILE!"
echo ===== 设备自检完成 ===== >> "!RESULT_FILE!"
echo 退出码: !CHECK_RESULT! >> "!RESULT_FILE!"

call :log [INFO] 自检退出码: !CHECK_RESULT!

if "!CHECK_RESULT!"=="0" (
    call :log [INFO] 自检结果：全部通过
) else (
    call :log [INFO] 自检结果：存在失败项
)

call :CleanupRemote
call :log [INFO] 完整输出已保存至: !RESULT_FILE!
call :PauseExit
exit /b !CHECK_RESULT!

rem ===================== Subroutine: Dual-output log =====================
rem Log to both console and the result file.
rem Usage: call :log [INFO] message...
:log
echo %* >> "!RESULT_FILE!"
echo %*
goto :eof

rem ===================== Subroutine: Remote cleanup =====================
rem Clean up remote testcheck directory on exit (safety net).
rem This ensures leftover files are removed even if pcba_check.sh trap didn't fire.
:CleanupRemote
!SSH_CMD! "echo !ROOT_PASSWORD! > /tmp/.sudo_pwd && sudo -S rm -rf !REMOTE_TESTCHECK_DIR! < /tmp/.sudo_pwd && rm -f /tmp/.sudo_pwd" >nul 2>&1
goto :eof

rem ===================== Subroutine: Countdown before exit =====================
rem For double-click runs: keep the window open. timeout /t -1 waits
rem indefinitely until any key is pressed (no auto-close).
:PauseExit
echo.
timeout /t -1
goto :eof

rem ===================== Subroutine: Build pcba_check.sh remote command =====================
rem Initializes PCBA_CMD and REMOTE_LOG.
rem Design:
rem   1. echo password | sudo -S  -- password fed via remote-side pipe (reliable)
rem   2. set -o pipefail keeps the real pcba_check.sh exit code through the pipe
rem   3. tee saves the UTF-8 stream to REMOTE_LOG for later download
rem   4. PCBA_EXIT=N is echoed and appended to REMOTE_LOG so the local side can
rem      parse the real exit code from the downloaded log
:InitPcbaCmd
set "REMOTE_LOG=/tmp/pcba_check_output.log"
set "PCBA_CMD=set -o pipefail; echo !ROOT_PASSWORD! | sudo -S bash !REMOTE_TESTCHECK_DIR!/scripts/pcba_check.sh --vendor !VENDOR! --testcheck-dir !REMOTE_TESTCHECK_DIR! 2>&1 | tee !REMOTE_LOG!; RC=$?; echo PCBA_EXIT=$RC | tee -a !REMOTE_LOG!"
goto :eof

rem ===================== Subroutine: Run pcba_check.sh (UTF-8 console output) =====================
rem Uses run_remote.ps1 (same directory) which reads plink's stdout as a RAW
rem byte stream and decodes it with a STATEFUL UTF-8 decoder:
rem   - complete UTF-8 sequences are displayed immediately (real-time)
rem   - incomplete trailing bytes are buffered and joined with the next chunk
rem   -> fixes broken Chinese characters caused by chunk-boundary splits.
rem Variables are passed via environment (PLINK_PATH/SSH_PASSWORD/SSH_PORT/
rem SSH_TARGET/PCBA_CMD). Exit code is parsed from PCBA_EXIT=N in the log.
:RunPcbaCheck
if "!USE_PLINK!"=="1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%run_remote.ps1"
) else (
    rem OpenSSH fallback: run directly (may show rare partial-char garbage)
    !SSH_CMD! "!PCBA_CMD!"
)
goto :eof

rem ===================== Subroutine: Show Help =====================
:show_help
echo BM1684x Edge Device One-Click Self-Check Script (Windows)
echo.
echo Function: Upload scripts and config to /data/testcheck/, remotely invoke pcba_check.sh
echo           Unified hardware self-check + SCM install + SCM self-check, real-time output.
echo.
echo Dependencies: Windows built-in OpenSSH or PuTTY (putty.org)
echo Recommended: Place plink.exe and pscp.exe in the same directory, no interaction needed.
echo.
echo Usage:
echo   run_check.bat                                  Use default IP, username, and password
echo   run_check.bat 192.168.112.47 linaro            Specify IP and username
echo   run_check.bat --password mypwd                 Manually set SSH password
echo   run_check.bat --root-pwd rpwd                  Manually set root password
echo   run_check.bat --vendor huayu                   Specify device vendor (default: huayu)
echo   run_check.bat --deb qifeng-scm-xxx.deb         Specify deb package path
echo   run_check.bat -h                               Show this help
echo.
echo Parameters:
echo   [IP]                    Device IP address (default: 192.168.112.47)
echo   [USER]                  SSH username (default: linaro)
echo   --password PWD          SSH login password (default: linaro, or env SSH_PASSWORD)
echo   --root-pwd PWD          Root password (default: linaro, or env ROOT_PASSWORD)
echo   --vendor NAME           Device vendor name (default: huayu, determines which test script to use)
echo   --deb FILE              Deb package path (default: auto-match qifeng-scm-*.deb in current dir)
echo   -h, --help              Show this help message
echo.
echo Examples:
echo   run_check.bat
echo   run_check.bat 192.168.112.162 linaro --password mypwd
echo   run_check.bat --vendor huayu
echo   set SSH_PASSWORD=mypwd ^&^& run_check.bat
echo.
echo Full Flow: Upload files -^> Execute pcba_check.sh -^> Real-time output -^> Save to check_result.txt
exit /b 0
