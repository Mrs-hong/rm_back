@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

rem Windows 端 BM1684x 边缘设备一键自检脚本
rem 依赖：Windows 10/11 内置 OpenSSH（ssh.exe / scp.exe）
rem 用法：
rem   run_check.bat                        使用默认值
rem   run_check.bat 192.168.112.47 linaro  自定义 IP 和用户名

rem ===================== 参数默认值 =====================
set "DEVICE_IP=%~1"
if "%~1"=="" set "DEVICE_IP=192.168.112.47"

set "SSH_USER=%~2"
if "%~2"=="" set "SSH_USER=linaro"

rem 密码建议通过环境变量传入，避免硬编码；未设置时使用默认值
if not defined SSH_PASSWORD set "SSH_PASSWORD=linaro"
if not defined ROOT_PASSWORD set "ROOT_PASSWORD=linaro"

set "REMOTE_DEB_PATH=/tmp/qifeng-scm.deb"
set "REMOTE_CONFIG_DIR=/etc/qifeng-scm"
set "REMOTE_CONFIG_PATH=%REMOTE_CONFIG_DIR%/selftest.json"
set "REMOTE_REPORT_PATH=/var/log/qifeng-scm/selftest-report.json"
set "LOCAL_LOG_DIR=logs"
set "SCP_OPTS=-o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL"
set "SSH_OPTS=-o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL"

rem ===================== 前置检查 =====================
echo [INFO] 目标设备: %SSH_USER%@%DEVICE_IP%

where ssh.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] 未找到 ssh.exe，请确认 Windows OpenSSH 客户端已安装并加入 PATH。
    exit /b 1
)
where scp.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] 未找到 scp.exe，请确认 Windows OpenSSH 客户端已安装并加入 PATH。
    exit /b 1
)

rem ===================== 查找 deb 包 =====================
set "DEB_FILE="
for /f "delims=" %%a in ('dir /b /o-d "qifeng-scm_*.deb" 2^>nul') do (
    if not defined DEB_FILE (
        set "DEB_FILE=%%a"
    )
)
if not defined DEB_FILE (
    echo [ERROR] 当前目录未找到 qifeng-scm_*.deb，请将 deb 包与 bat 放在同一目录。
    exit /b 1
)
echo [INFO] 使用 deb 包: %DEB_FILE%

rem ===================== 创建本地日志目录 =====================
if not exist "%LOCAL_LOG_DIR%" mkdir "%LOCAL_LOG_DIR%"

rem ===================== 上传 deb 包 =====================
echo [INFO] 上传 deb 包到设备...
scp.exe %SCP_OPTS% -P 22 "%DEB_FILE%" %SSH_USER%@%DEVICE_IP%:%REMOTE_DEB_PATH%
if errorlevel 1 (
    echo [ERROR] 上传 deb 包失败。
    exit /b 1
)

rem ===================== 安装 deb 包 =====================
echo [INFO] 安装 qifeng-scm...
echo %ROOT_PASSWORD% | ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "sudo -S dpkg -i %REMOTE_DEB_PATH% && sudo -S apt-get install -f -y"
if errorlevel 1 (
    echo [ERROR] 安装 qifeng-scm 失败。
    call :pull_remote_log
    exit /b 1
)

rem ===================== 等待服务就绪 =====================
echo [INFO] 等待 qf_scmd 服务就绪...
set /a WAIT_COUNT=0
:wait_loop
ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "systemctl is-active qifeng-scmd" >nul 2>&1
if not errorlevel 1 goto service_ready
if %WAIT_COUNT% geq 30 (
    echo [ERROR] 等待 qf_scmd 服务超时。
    call :pull_remote_log
    exit /b 1
)
timeout /t 2 /nobreak >nul
set /a WAIT_COUNT+=1
goto wait_loop
:service_ready
echo [INFO] qf_scmd 服务已就绪。

rem ===================== 确保远端配置目录存在 =====================
echo %ROOT_PASSWORD% | ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "sudo -S mkdir -p %REMOTE_CONFIG_DIR%" >nul 2>&1

rem ===================== 上传 selftest.json =====================
echo [INFO] 上传 selftest.json...
scp.exe %SCP_OPTS% -P 22 "config\selftest.json" %SSH_USER%@%DEVICE_IP%:%REMOTE_CONFIG_PATH%
if errorlevel 1 (
    echo [ERROR] 上传 selftest.json 失败。
    call :pull_remote_log
    exit /b 1
)

rem ===================== 上传 pcba 脚本 =====================
echo [INFO] 上传 pcba 检测脚本...
echo %ROOT_PASSWORD% | ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "sudo -S mkdir -p /opt/qifeng-scm/scripts" >nul 2>&1
scp.exe %SCP_OPTS% -P 22 "scripts\pcba_check.sh" %SSH_USER%@%DEVICE_IP%:/tmp/pcba_check.sh
if errorlevel 1 (
    echo [ERROR] 上传 pcba_check.sh 失败。
    exit /b 1
)
echo %ROOT_PASSWORD% | ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "sudo -S mv /tmp/pcba_check.sh /opt/qifeng-scm/scripts/pcba_check.sh && sudo -S chmod +x /opt/qifeng-scm/scripts/pcba_check.sh" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] 设置 pcba_check.sh 权限失败。
    exit /b 1
)

rem ===================== 执行自检 =====================
echo [INFO] 开始执行硬件自检...
ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "qf_scmc check --config %REMOTE_CONFIG_PATH%"
set "CHECK_RESULT=%ERRORLEVEL%"

rem ===================== 拉回报告 =====================
echo [INFO] 拉回自检报告...
scp.exe %SCP_OPTS% -P 22 %SSH_USER%@%DEVICE_IP%:%REMOTE_REPORT_PATH% "%LOCAL_LOG_DIR%\selftest-report-%DEVICE_IP%.json" >nul 2>&1
if errorlevel 1 (
    echo [WARN] 拉回自检报告失败，可能自检未生成报告。
)

if %CHECK_RESULT% neq 0 (
    echo [ERROR] 自检执行失败或存在关键项未通过，详情请查看上方输出或 %LOCAL_LOG_DIR%\selftest-report-%DEVICE_IP%.json
    call :pull_remote_log
    exit /b 1
)

echo [INFO] 自检完成，结果已保存到 %LOCAL_LOG_DIR%\selftest-report-%DEVICE_IP%.json
exit /b 0

rem ===================== 子程序：拉取远端日志 =====================
:pull_remote_log
echo [INFO] 尝试拉取远端服务日志...
ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "journalctl -u qifeng-scmd -n 50 --no-pager" > "%LOCAL_LOG_DIR%\qf_scmd-%DEVICE_IP%.log" 2>&1
exit /b 0
