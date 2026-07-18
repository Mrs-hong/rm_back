@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion
rem ==========================================================================
rem setup_network.bat — 一次性网卡配置助手（需管理员权限）
rem
rem 功能：
rem   为"连接设备的网卡"追加一个 192.168.112.250/24 副地址，使 Windows 主机
rem   能直接路由到设备 (192.168.112.223)。副地址非破坏性，不影响主网络/上网。
rem
rem 何时运行：
rem   - 首次部署时运行一次（IT 人员操作）
rem   - 设备换了网口或换了主机后重新运行
rem   - 之后日常 run.bat 不需要管理员权限
rem
rem 设备 IP 取自 config.ini [device] ip=
rem   Windows 副地址 = 设备 IP 的前三段 + ".250"
rem ==========================================================================
cd /d "%~dp0"

rem === Step 1: 检查管理员权限 ===
net session >nul 2>&1
if !errorlevel! neq 0 (
    echo [ERROR] 需要管理员权限运行。
    echo   请右键 setup_network.bat，选择 "以管理员身份运行"。
    pause
    exit /b 1
)

rem === Step 2: 读取设备 IP 并计算 Windows 副地址 ===
set "INI_FILE=%~dp0config.ini"
set "DEVICE_IP=192.168.112.223"
if exist "!INI_FILE!" (
    call lib\ini.bat get device ip DEVICE_IP
)
if not defined DEVICE_IP set "DEVICE_IP=192.168.112.223"
for /f "tokens=1-3 delims=." %%a in ("!DEVICE_IP!") do set "_net=%%a.%%b.%%c"
set "WIN_IP=!_net!.250"
echo [INFO] 设备 IP: !DEVICE_IP!
echo [INFO] 将为选定网卡追加副地址: !WIN_IP!/255.255.255.0
echo.

rem === Step 3: 列出"已连接"的网卡 ===
echo 已连接的网卡列表:
echo --------------------------------------------------
set "_count=0"
set "_cand_names="
for /f "skip=3 tokens=1,2,3,*" %%a in ('netsh interface show interface') do (
    set "_admin=%%a"
    set "_state=%%b"
    set "_name=%%d"
    rem 跳过空行
    if "!_state!"=="Connected" (
        set /a "_count+=1"
        echo   !_count!. !_name!
        set "_cand_!_count!=!_name!"
    )
)

if !_count! equ 0 (
    echo [ERROR] 未找到已连接的网卡。请确认网线已插好、网卡已启用。
    pause
    exit /b 1
)

rem === Step 4: 选择网卡 ===
set "_pick=1"
if !_count! gtr 1 (
    echo.
    set /p "_pick=请输入网卡序号(默认 1): "
    if "!_pick!"=="" set "_pick=1"
)
rem 取出选中的网卡名
set "NIC_NAME=!_cand_%_pick%!"
if "!NIC_NAME!"=="" (
    echo [ERROR] 选择无效
    pause
    exit /b 1
)
echo.
echo [INFO] 已选网卡: !NIC_NAME!

rem === Step 5: 检查该网卡是否已有 192.168.112.x 地址（幂等）===
echo [INFO] 检查现有 IP 配置...
set "_already=0"
for /f "tokens=*" %%L in ('netsh interface ipv4 show addresses "!NIC_NAME!" 2^>nul ^| findstr /r "!_net!\."') do (
    set "_already=1"
    echo   已存在: %%L
)
if !_already! equ 1 (
    echo [INFO] 网卡已配置 !_net!.x 地址，无需重复添加。
    goto :verify_ping
)

rem === Step 6: 追加副地址 ===
echo [INFO] 追加副地址 !WIN_IP!/255.255.255.0 ...
netsh interface ipv4 add address "!NIC_NAME!" !WIN_IP! 255.255.255.0 >nul 2>&1
if !errorlevel! neq 0 (
    rem 部分版本接受 address=/mask= 关键字语法，再试一次
    netsh interface ipv4 add address name="!NIC_NAME!" address=!WIN_IP! mask=255.255.255.0 >nul 2>&1
)
if !errorlevel! neq 0 (
    echo [ERROR] 添加副地址失败。请检查网卡名称或手动配置。
    pause
    exit /b 1
)
echo [INFO] 副地址添加成功

:verify_ping
echo.
echo [INFO] 验证设备可达性（ping !DEVICE_IP!）...
ping -n 2 -w 1000 "!DEVICE_IP!" | findstr /i "TTL=" >nul 2>&1
if !errorlevel! equ 0 (
    echo.
    echo ============================================================
    echo   配置成功！现在可以双击 run.bat 进行设备自检。
    echo ============================================================
    powershell -NoProfile -Command "[console]::beep(800,300)" >nul 2>&1
    pause
    exit /b 0
) else (
    echo.
    echo [WARN] ping 设备失败。可能原因：
    echo   1. 设备未开机或网线未插好
    echo   2. 设备 IP 不是 !DEVICE_IP!（修改 config.ini [device] ip=）
    echo   3. 选择的网卡不对（重新运行本脚本选其它网卡）
    echo.
    echo 副地址已配置，可先排查设备侧，之后双击 run.bat 即可。
    pause
    exit /b 0
)
