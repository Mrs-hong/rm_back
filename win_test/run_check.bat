@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

rem ==========================================================================
rem Windows 端 BM1684x 边缘设备一键自检脚本
rem
rem 依赖：Windows 10/11 内置 OpenSSH（ssh.exe / scp.exe）
rem 首次运行会自动配置 SSH 密钥免密登录，后续无需输入密码
rem
rem 用法：
rem   run_check.bat                                  使用默认值
rem   run_check.bat 192.168.112.47 linaro            自定义 IP 和用户名
rem   run_check.bat --skip-install                   跳过安装，直接执行自检
rem ==========================================================================

rem ===================== 参数默认值 =====================
set "DEVICE_IP=192.168.112.47"
set "SSH_USER=linaro"
set "SKIP_INSTALL=0"

rem 解析命令行参数
:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="--skip-install" (
    set "SKIP_INSTALL=1"
    shift
    goto :parse_args
)
if not defined DEVICE_IP_SET (
    set "DEVICE_IP=%~1"
    set "DEVICE_IP_SET=1"
    shift
    goto :parse_args
)
if not defined SSH_USER_SET (
    set "SSH_USER=%~2"
    set "SSH_USER_SET=1"
    shift
    shift
    goto :parse_args
)
shift
goto :parse_args
:args_done

rem 密码建议通过环境变量传入，避免硬编码；未设置时使用默认值
if not defined SSH_PASSWORD set "SSH_PASSWORD=linaro"
if not defined ROOT_PASSWORD set "ROOT_PASSWORD=linaro"

set "REMOTE_DEB_PATH=/tmp/qifeng-scm.deb"
set "REMOTE_CONFIG_DIR=/etc/qifeng-scm"
set "REMOTE_CONFIG_PATH=%REMOTE_CONFIG_DIR%/selftest.json"
set "REMOTE_REPORT_PATH=/var/log/qifeng-scm/selftest-report.json"
set "REMOTE_SCRIPTS_DIR=/opt/qifeng-scm/scripts"
set "REMOTE_PCBA_SCRIPT=%REMOTE_SCRIPTS_DIR%/pcba_check.sh"
set "LOCAL_LOG_DIR=logs"
set "SSH_KEY_DIR=%USERPROFILE%\.ssh"
set "SSH_KEY_FILE=%SSH_KEY_DIR%\id_rsa_qifeng"
set "SCP_OPTS=-o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL"
set "SSH_OPTS=-o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL"

rem ===================== 前置检查 =====================
echo [INFO] 目标设备: %SSH_USER%@%DEVICE_IP%

where ssh.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] 未找到 ssh.exe，请确认 Windows OpenSSH 客户端已安装并加入 PATH。
    echo [INFO] 也可使用 Python 脚本: python run_check.py
    exit /b 1
)
where scp.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] 未找到 scp.exe，请确认 Windows OpenSSH 客户端已安装并加入 PATH。
    echo [INFO] 也可使用 Python 脚本: python run_check.py
    exit /b 1
)

rem ===================== SSH 密钥配置 =====================
rem 首次运行时自动生成密钥并推送到设备，实现后续免密登录
call :setup_ssh_key

rem 密钥配置成功后，SSH/SCP 命令使用 -i 指定密钥
set "SCP_OPTS=%SCP_OPTS% -i %SSH_KEY_FILE%"
set "SSH_OPTS=%SSH_OPTS% -i %SSH_KEY_FILE%"

rem ===================== 创建本地日志目录 =====================
if not exist "%LOCAL_LOG_DIR%" mkdir "%LOCAL_LOG_DIR%"

rem ===================== 查找 deb 包 =====================
if "%SKIP_INSTALL%"=="0" (
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

    rem ===================== 上传 deb 包 =====================
    echo [INFO] 上传 deb 包到设备...
    scp.exe %SCP_OPTS% -P 22 "%DEB_FILE%" %SSH_USER%@%DEVICE_IP%:%REMOTE_DEB_PATH%
    if errorlevel 1 (
        echo [ERROR] 上传 deb 包失败。
        exit /b 1
    )

    rem ===================== 安装 deb 包 =====================
    echo [INFO] 安装 qifeng-scm...
    ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "sudo -S dpkg -i %REMOTE_DEB_PATH% && sudo -S apt-get install -f -y" <nul
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
) else (
    echo [INFO] 跳过安装步骤（--skip-install）
)

rem ===================== 确保远端配置目录存在 =====================
ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "sudo -S mkdir -p %REMOTE_CONFIG_DIR% && sudo -S mkdir -p %REMOTE_SCRIPTS_DIR%" <nul >nul 2>&1

rem ===================== 上传 selftest.json =====================
echo [INFO] 上传 selftest.json...
scp.exe %SCP_OPTS% -P 22 "config\selftest.json" %SSH_USER%@%DEVICE_IP%:/tmp/selftest.json
if errorlevel 1 (
    echo [ERROR] 上传 selftest.json 失败。
    call :pull_remote_log
    exit /b 1
)
ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "sudo -S mv /tmp/selftest.json %REMOTE_CONFIG_PATH% && sudo -S chmod 644 %REMOTE_CONFIG_PATH%" <nul >nul 2>&1

rem ===================== 上传 pcba 脚本 =====================
echo [INFO] 上传 pcba 检测脚本...
scp.exe %SCP_OPTS% -P 22 "scripts\pcba_check.sh" %SSH_USER%@%DEVICE_IP%:/tmp/pcba_check.sh
if errorlevel 1 (
    echo [ERROR] 上传 pcba_check.sh 失败。
    exit /b 1
)
ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "sudo -S mv /tmp/pcba_check.sh %REMOTE_PCBA_SCRIPT% && sudo -S chmod +x %REMOTE_PCBA_SCRIPT%" <nul >nul 2>&1
if errorlevel 1 (
    echo [ERROR] 设置 pcba_check.sh 权限失败。
    exit /b 1
)

rem ===================== 执行自检 =====================
echo [INFO] 开始执行硬件自检...
echo ========================================
ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "qf_scmc check --config %REMOTE_CONFIG_PATH%"
set "CHECK_RESULT=%ERRORLEVEL%"
echo ========================================

rem ===================== 拉回报告 =====================
echo [INFO] 拉回自检报告...
scp.exe %SCP_OPTS% -P 22 %SSH_USER%@%DEVICE_IP%:%REMOTE_REPORT_PATH% "%LOCAL_LOG_DIR%\selftest-report-%DEVICE_IP%.json" >nul 2>&1
if errorlevel 1 (
    echo [WARN] 拉回自检报告失败，可能自检未生成报告。
)

if %CHECK_RESULT% neq 0 (
    echo [ERROR] 自检执行失败或存在关键项未通过
    echo [INFO] 详情请查看上方输出或 %LOCAL_LOG_DIR%\selftest-report-%DEVICE_IP%.json
    call :pull_remote_log
    exit /b 1
)

echo [INFO] 自检完成，结果已保存到 %LOCAL_LOG_DIR%\selftest-report-%DEVICE_IP%.json
exit /b 0

rem ===================== 子程序：配置 SSH 密钥 =====================
:setup_ssh_key
rem 检查是否已有密钥
if not exist "%SSH_KEY_DIR%" mkdir "%SSH_KEY_DIR%"

rem 如果密钥不存在，生成一个
if not exist "%SSH_KEY_FILE%" (
    echo [INFO] 生成 SSH 密钥（首次运行）...
    ssh-keygen.exe -t rsa -b 2048 -f "%SSH_KEY_FILE%" -N "" -q
    if errorlevel 1 (
        echo [WARN] SSH 密钥生成失败，将使用密码方式登录。
        exit /b 0
    )
)

rem 尝试将公钥推送到设备（需要输入一次密码）
ssh.exe -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL -o PasswordAuthentication=yes -o PubkeyAuthentication=no -p 22 %SSH_USER%@%DEVICE_IP% "exit" >nul 2>&1
if errorlevel 1 (
    echo [INFO] 首次连接设备，配置 SSH 密钥免密登录...
    echo [INFO] 请输入设备密码（仅此一次）:
    type "%SSH_KEY_FILE%.pub" | ssh.exe -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL -p 22 %SSH_USER%@%DEVICE_IP% "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys && chmod 700 ~/.ssh"
    if errorlevel 1 (
        echo [WARN] SSH 密钥配置失败，将使用密码方式登录。
        exit /b 0
    )
    echo [INFO] SSH 密钥配置成功，后续免密登录。
)
exit /b 0

rem ===================== 子程序：拉取远端日志 =====================
:pull_remote_log
echo [INFO] 尝试拉取远端服务日志...
ssh.exe %SSH_OPTS% -p 22 %SSH_USER%@%DEVICE_IP% "journalctl -u qifeng-scmd -n 50 --no-pager" > "%LOCAL_LOG_DIR%\qf_scmd-%DEVICE_IP%.log" 2>&1
exit /b 0
