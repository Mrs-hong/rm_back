@echo off
rem ==========================================================================
rem lib/net_check.bat — 网络可达性预检
rem
rem 前置环境变量（由 run.bat 设置）：
rem   DEVICE_IP   设备 IP
rem
rem 调用方式：
rem   call lib\net_check.bat reachable
rem   返回 0=可达，1=不可达（已打印友好提示）
rem ==========================================================================
goto :%1 2>nul
echo [ERROR] lib\net_check.bat: 未知函数 '%~1' 1>&2
exit /b 99

rem ===================== 函数: reachable =====================
rem 用 ping 探测设备 IP 是否可达；区分"网线未插/IP未配"和"设备关机"
:reachable
setlocal EnableDelayedExpansion
set "_ip=%DEVICE_IP%"

rem ping -n 2 -w 1000：2 个包，每个 1000ms 超时；用 findstr TTL 确认真实回包
ping -n 2 -w 1000 "!_ip!" | findstr /i "TTL=" >nul 2>&1
if !errorlevel! equ 0 (
    endlocal
    exit /b 0
)

rem 不可达：尝试进一步诊断
echo.
echo [ERROR] 无法连接设备 !_ip!
echo --------------------------------------------------
echo 可能原因与排查：
echo   1. 网线未插好 / 设备未开机
echo      - 检查网线指示灯是否亮起
echo      - 确认设备已通电启动
echo   2. Windows 主机侧未配置 192.168.112.x 网段地址
echo      - 首次使用请右键 setup_network.bat "以管理员身份运行"
echo      - 该脚本会为连接设备的网卡追加 192.168.112.250/24 副地址（不影响主网络）
echo   3. 设备 IP 与 config.ini 中 [device] ip= 不一致
echo      - 修改 config.ini 中 ip= 行为设备实际 IP
echo --------------------------------------------------
endlocal
exit /b 1

rem ===================== 函数: wait_online =====================
rem 参数: %2=最大等待秒数
rem 循环 ping 直到可达或超时；用于设备刚开机等待
:wait_online
setlocal EnableDelayedExpansion
set "_max=%~2"
if "!_max!"=="" set "_max=30"
set "_elapsed=0"
:wait_loop
ping -n 1 -w 1000 "%DEVICE_IP%" | findstr /i "TTL=" >nul 2>&1
if !errorlevel! equ 0 (
    endlocal
    exit /b 0
)
set /a "_elapsed+=2"
if !_elapsed! lss !_max! (
    echo [INFO] 等待设备上线... (!_elapsed!s/!_max!s)
    timeout /t 2 /nobreak >nul
    goto :wait_loop
)
endlocal
exit /b 1
