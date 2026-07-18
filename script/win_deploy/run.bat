@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion
rem ==========================================================================
rem run.bat — win_deploy 主入口（双击运行 / 命令行调用均可）
rem
rem 流程：
rem   1. 解析 config.ini（device/paths/timing/production）
rem   2. 生成带时间戳的结果文件
rem   3. ping 预检 → SSH hostkey 缓存 → 抓设备 SN
rem   4. 远程清理 + 建目录 + 上传 config/scripts/deb/sha256
rem   5. 远程执行 pcba_check.sh（流式 UTF-8 + 全局超时）
rem   6. 下载远程日志 → 追加到结果文件
rem   7. 醒目 PASS/FAIL 横幅 + 追加 batch_log.csv
rem   8. 倒计时循环下一台
rem
rem 设计要点：
rem   - 仅依赖随包 tools\plink.exe / tools\pscp.exe（Win7/10/11 全兼容）
rem   - 不写 root 密码到任何文件（sudo -S 通过 stdin 管道喂入）
rem   - 全程超时保护，绝不永久挂起
rem   - 工人永不修改 bat，仅 IT 改 config.ini
rem ==========================================================================

rem === 强制工作目录为脚本目录（双击运行兼容）===
cd /d "%~dp0"

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "INI_FILE=%SCRIPT_DIR%\config.ini"
set "PLINK_PATH=%SCRIPT_DIR%\tools\plink.exe"
set "PSCP_PATH=%SCRIPT_DIR%\tools\pscp.exe"
set "PS_DIR=%SCRIPT_DIR%\ps"
set "LIB_DIR=%SCRIPT_DIR%\lib"

rem === 校验必要文件存在 ===
if not exist "%PLINK_PATH%" (
    echo [ERROR] 缺少 tools\plink.exe
    pause
    exit /b 1
)
if not exist "%PSCP_PATH%" (
    echo [ERROR] 缺少 tools\pscp.exe
    pause
    exit /b 1
)
if not exist "%INI_FILE%" (
    echo [ERROR] 缺少 config.ini
    pause
    exit /b 1
)

rem === 解析 config.ini ===
call lib\ini.bat get device ip DEVICE_IP
call lib\ini.bat get device ssh_user SSH_USER
call lib\ini.bat get device ssh_password SSH_PASSWORD
call lib\ini.bat get device root_password ROOT_PASSWORD
call lib\ini.bat get device ssh_port SSH_PORT
if not defined SSH_PORT set "SSH_PORT=22"
call lib\ini.bat get device vendor VENDOR
if not defined VENDOR set "VENDOR=huayu"

call lib\ini.bat get paths remote_testcheck_dir REMOTE_TESTCHECK_DIR
if not defined REMOTE_TESTCHECK_DIR set "REMOTE_TESTCHECK_DIR=/data/testcheck"
call lib\ini.bat get paths remote_log REMOTE_LOG
if not defined REMOTE_LOG set "REMOTE_LOG=/tmp/pcba_check_output.log"
call lib\ini.bat get paths local_deb_dir LOCAL_DEB_DIR
if not defined LOCAL_DEB_DIR set "LOCAL_DEB_DIR=deb"
call lib\ini.bat get paths local_results_dir LOCAL_RESULTS_DIR
if not defined LOCAL_RESULTS_DIR set "LOCAL_RESULTS_DIR=results"

call lib\ini.bat get timing exec_timeout_sec EXEC_TIMEOUT_SEC
if not defined EXEC_TIMEOUT_SEC set "EXEC_TIMEOUT_SEC=600"
call lib\ini.bat get timing single_script_timeout_sec SINGLE_SCRIPT_TIMEOUT_SEC
if not defined SINGLE_SCRIPT_TIMEOUT_SEC set "SINGLE_SCRIPT_TIMEOUT_SEC=120"
call lib\ini.bat get timing ssh_retry SSH_RETRY
if not defined SSH_RETRY set "SSH_RETRY=3"
call lib\ini.bat get timing scp_retry SCP_RETRY
if not defined SCP_RETRY set "SCP_RETRY=3"
call lib\ini.bat get timing retry_backoff_base RETRY_BACKOFF_BASE
if not defined RETRY_BACKOFF_BASE set "RETRY_BACKOFF_BASE=2"

call lib\ini.bat get production auto_next AUTO_NEXT
if not defined AUTO_NEXT set "AUTO_NEXT=true"
call lib\ini.bat get production auto_next_delay_sec AUTO_NEXT_DELAY_SEC
if not defined AUTO_NEXT_DELAY_SEC set "AUTO_NEXT_DELAY_SEC=5"
call lib\ini.bat get production beep_on_finish BEEP_ON_FINISH
if not defined BEEP_ON_FINISH set "BEEP_ON_FINISH=true"

set "SSH_TARGET=%SSH_USER%@%DEVICE_IP%"

rem ==========================================================================
rem 主循环（每轮处理一台设备）
rem ==========================================================================
:device_loop

rem === 生成时间戳与结果文件路径 ===
for /f "usebackq delims=" %%T in (`powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"`) do set "_ts=%%T"
set "_ip_safe=%DEVICE_IP:.=_%"
set "RESULT_FILE=%SCRIPT_DIR%\%LOCAL_RESULTS_DIR%\check_!_ts!_!_ip_safe!.txt"
set "BATCH_LOG=%SCRIPT_DIR%\%LOCAL_RESULTS_DIR%\batch_log.csv"

call lib\log.bat init "!RESULT_FILE!"

call lib\log.bat separator "设备自检开始"
call lib\log.bat log "[INFO] 设备: !SSH_USER!@!DEVICE_IP!:!SSH_PORT! 厂商:!VENDOR!"
call lib\log.bat log "[INFO] 时间: !_ts!"
call lib\log.bat log "[INFO] 远程目录: !REMOTE_TESTCHECK_DIR!"

rem === Step 1: ping 预检 ===
call lib\log.bat log "[INFO] 检查设备可达性..."
call lib\net_check.bat reachable
if !errorlevel! neq 0 (
    call lib\log.bat log "[ERROR] 设备不可达，跳过此设备"
    call :finalize FAIL "设备不可达"
    goto :ask_next
)

rem === Step 2: SSH hostkey 缓存 + 凭据验证 ===
call lib\log.bat log "[INFO] 验证 SSH 凭据..."
call lib\ssh.bat init
if !errorlevel! neq 0 (
    call lib\log.bat log "[ERROR] SSH 凭据验证失败"
    call :finalize FAIL "SSH验证失败"
    goto :ask_next
)

rem === Step 3: 抓取设备序列号（用于 batch_log.csv 追溯）===
set "DEVICE_SN=UNKNOWN"
for /f "usebackq delims=" %%S in (`""!PLINK_PATH!" -batch -pw "!SSH_PASSWORD!" -P !SSH_PORT! !SSH_USER!@!DEVICE_IP! "for p in /etc/device-sn /proc/device-tree/serial-number /etc/hostname; do [ -f \"$p\" ] && head -c 64 \"$p\" 2>/dev/null | tr -d '\\0\\n\\r' && break; done"`) do (
    if "!DEVICE_SN!"=="UNKNOWN" set "DEVICE_SN=%%S"
)
call lib\log.bat log "[INFO] 设备 SN: !DEVICE_SN!"

rem === Step 4: 远程清理旧文件（避免残留数据影响本次）===
call lib\log.bat log "[INFO] 清理远程旧文件..."
call lib\ssh.bat exec "echo '!ROOT_PASSWORD!' | sudo -S rm -rf !REMOTE_TESTCHECK_DIR! /data/check /tmp/pcba_check_output.log /tmp/scm_selftest_output.log /tmp/scm_install.log /tmp/eth_perf_detail.log /tmp/fio_press_write_*.log /tmp/fio_press_read*.log 2>/dev/null"
rem 清理不影响后续流程，不检查 errorlevel

rem === Step 5: 创建远程目录（root 权限）===
call lib\log.bat log "[INFO] 创建远程目录..."
call lib\ssh.bat exec "echo '!ROOT_PASSWORD!' | sudo -S sh -c 'mkdir -p !REMOTE_TESTCHECK_DIR!/config !REMOTE_TESTCHECK_DIR!/scripts/!VENDOR! && chown -R !SSH_USER!:!SSH_USER! !REMOTE_TESTCHECK_DIR!'"
if !errorlevel! neq 0 (
    call lib\log.bat log "[ERROR] 创建远程目录失败"
    call :finalize FAIL "创建远程目录失败"
    goto :ask_next
)

rem === Step 6: 上传 config ===
call lib\log.bat log "[INFO] 上传配置文件..."
call lib\ssh.bat upload "config\selftest.json" "!REMOTE_TESTCHECK_DIR!/config/selftest.json"
if !errorlevel! neq 0 (
    call lib\log.bat log "[ERROR] 配置文件上传失败"
    call :finalize FAIL "配置上传失败"
    goto :ask_next
)

rem === Step 7: 上传 scripts ===
call lib\log.bat log "[INFO] 上传检测脚本..."
call lib\ssh.bat upload "-r scripts" "!REMOTE_TESTCHECK_DIR!/"
if !errorlevel! neq 0 (
    call lib\log.bat log "[ERROR] 检测脚本上传失败"
    call :finalize FAIL "脚本上传失败"
    goto :ask_next
)

rem === Step 8: 查找并上传 deb 包 ===
set "DEB_FILE="
for /f "delims=" %%a in ('dir /b /o-d "%LOCAL_DEB_DIR%\qifeng-scm*.deb" 2^>nul') do (
    if not defined DEB_FILE set "DEB_FILE=%LOCAL_DEB_DIR%\%%a"
)
if not defined DEB_FILE (
    call lib\log.bat log "[ERROR] 未在 %LOCAL_DEB_DIR% 下找到 qifeng-scm*.deb"
    call :finalize FAIL "deb包未找到"
    goto :ask_next
)
call lib\log.bat log "[INFO] 安装包: !DEB_FILE!"
call lib\ssh.bat upload "!DEB_FILE!" "!REMOTE_TESTCHECK_DIR!/"
if !errorlevel! neq 0 (
    call lib\log.bat log "[ERROR] 安装包上传失败"
    call :finalize FAIL "deb上传失败"
    goto :ask_next
)

rem === Step 9: 查找并上传 sha256 ===
set "SHA256_FILE=!DEB_FILE!.sha256"
if not exist "!SHA256_FILE!" (
    set "SHA256_FILE="
    for /f "delims=" %%a in ('dir /b /o-d "%LOCAL_DEB_DIR%\qifeng-scm-*.deb.sha256" 2^>nul') do (
        if not defined SHA256_SET (
            set "SHA256_FILE=%LOCAL_DEB_DIR%\%%a"
            set "SHA256_SET=1"
        )
    )
)
if defined SHA256_FILE (
    call lib\log.bat log "[INFO] 上传校验文件: !SHA256_FILE!"
    call lib\ssh.bat upload "!SHA256_FILE!" "!REMOTE_TESTCHECK_DIR!/"
    if !errorlevel! neq 0 (
        call lib\log.bat log "[WARN] 校验文件上传失败，继续（设备端会跳过完整性检查）"
    )
) else (
    call lib\log.bat log "[WARN] 未找到校验文件，跳过完整性检查"
)

rem === Step 10: 设置脚本执行权限 ===
call lib\ssh.bat exec "chmod +x !REMOTE_TESTCHECK_DIR!/scripts/*.sh !REMOTE_TESTCHECK_DIR!/scripts/!VENDOR!/*.sh" >nul 2>&1

rem === Step 11: 依赖预检 + 可选离线包上传 ===
if exist "offline-debs\install_offline.sh" (
    call lib\log.bat log "[INFO] 检测 SCM 依赖..."
    call lib\ssh.bat exec "bash !REMOTE_TESTCHECK_DIR!/scripts/check_scm_deps.sh" >nul 2>&1
    if !errorlevel! neq 0 (
        call lib\log.bat log "[INFO] 缺少依赖，上传离线包..."
        call lib\ssh.bat upload "-r offline-debs" "!REMOTE_TESTCHECK_DIR!/"
        if !errorlevel! neq 0 (
            call lib\log.bat log "[ERROR] 离线包上传失败"
            call :finalize FAIL "离线包上传失败"
            goto :ask_next
        )
    ) else (
        call lib\log.bat log "[INFO] 所有依赖已安装"
    )
)

call lib\log.bat log "[INFO] 文件上传完成"
call lib\log.bat log ""

rem === Step 12: 远程执行 pcba_check.sh（流式 UTF-8 + 全局超时）===
call lib\log.bat log "[INFO] 开始设备自检（实时输出）..."
call lib\log.bat file "============================================================"

rem 构造远程命令：root 密码通过 stdin 管道喂给 sudo -S，绝不落盘
rem   set -o pipefail：保证 | tee 不会掩盖 pcba_check.sh 的真实退出码
rem   --script-timeout：把 [timing] single_script_timeout_sec 透传给设备端
rem   2>&1：合并 stderr（含 sudo 提示）到 stdout，统一 tee 到日志
set "PCBA_CMD=set -o pipefail; echo '!ROOT_PASSWORD!' | sudo -S bash !REMOTE_TESTCHECK_DIR!/scripts/pcba_check.sh --vendor !VENDOR! --testcheck-dir !REMOTE_TESTCHECK_DIR! --script-timeout !SINGLE_SCRIPT_TIMEOUT_SEC! 2>&1 | tee !REMOTE_LOG!"

call lib\ssh.bat exec_long "!PCBA_CMD!"
set "CHECK_RESULT=!errorlevel!"

call lib\log.bat file "============================================================"
call lib\log.bat log "[INFO] 自检退出码: !CHECK_RESULT!"

rem === Step 13: 下载远程完整日志，追加到本地结果文件 ===
call lib\log.bat log "[INFO] 下载完整输出..."
"!PSCP_PATH!" -batch -pw "!SSH_PASSWORD!" -P !SSH_PORT! !SSH_USER!@!DEVICE_IP!:!REMOTE_LOG! "!RESULT_FILE!.tmp" >nul 2>&1
if exist "!RESULT_FILE!.tmp" (
    rem 用 copy /b 把临时日志二进制追加到结果文件末尾
    copy /b "!RESULT_FILE!"+"!RESULT_FILE!.tmp" "!RESULT_FILE!" >nul
    del "!RESULT_FILE!.tmp" >nul
)

rem === Step 14: 远程清理（安全网，pcba_check.sh 的 trap 也会清理）===
call lib\ssh.bat exec "echo '!ROOT_PASSWORD!' | sudo -S rm -rf !REMOTE_TESTCHECK_DIR! 2>/dev/null" >nul 2>&1

rem === Step 15: 显示结果横幅 ===
if "!CHECK_RESULT!"=="0" (
    call :finalize PASS "全部检测项通过"
) else if "!CHECK_RESULT!"=="124" (
    call :finalize FAIL "执行超时被终止"
) else (
    call :finalize FAIL "退出码=!CHECK_RESULT!"
)

:ask_next
call lib\log.bat log "[INFO] 完整结果已保存: !RESULT_FILE!"

rem === Step 16: 循环下一台设备 ===
if /i not "!AUTO_NEXT!"=="true" goto :done
echo.
choice /c YN /t !AUTO_NEXT_DELAY_SEC! /d Y /m "检测下一台设备？(Y/N，!AUTO_NEXT_DELAY_SEC!秒后自动Y)"
if !errorlevel! equ 1 (
    echo.
    echo ------------------------------------------------------------
    echo   准备下一台：插好网线，确认设备已开机
    echo ------------------------------------------------------------
    timeout /t 2 /nobreak >nul
    goto :device_loop
)

:done
call lib\log.bat separator "本次作业结束"
call lib\banner.bat info "已完成，关闭窗口即可"
exit /b 0

rem ==========================================================================
rem 子程序: finalize — 显示结果横幅 + 写 batch_log.csv
rem 参数: %2=PASS/FAIL  %3=描述
rem ==========================================================================
:finalize
setlocal EnableDelayedExpansion
set "_result=%~2"
set "_desc=%~3"
set "_dur=?"
rem 写入 batch_log.csv（首行含表头）
if not exist "!BATCH_LOG!" echo timestamp,device_ip,device_sn,result,duration_sec,desc>"!BATCH_LOG!"
echo !_ts!,!DEVICE_IP!,!DEVICE_SN!,!_result!,!_dur!,!_desc!>>"!BATCH_LOG!"
rem 显示横幅
if "!_result!"=="PASS" (
    call lib\banner.bat pass "设备 !DEVICE_IP! (SN:!DEVICE_SN!)  — !_desc!"
    call lib\log.bat log "[INFO] 自检结果：PASS — !_desc!"
) else (
    call lib\banner.bat fail "设备 !DEVICE_IP! (SN:!DEVICE_SN!)  — !_desc!"
    call lib\log.bat log "[ERROR] 自检结果：FAIL — !_desc!"
)
endlocal
exit /b 0
