#!/bin/bash
# 极简蓝牙可用性验证脚本

# 颜色定义：检测 stdout 是否为 TTY，非 TTY 时禁用颜色码避免 ANSI 序列污染管道输出
if [ -t 1 ]; then
    G='\e[32m'; R='\e[31m'; N='\e[0m'
else
    G=''; R=''; N=''
fi

echo "================================="
echo " 蓝牙极简验证"
echo "================================="

# 1. 检查基础工具和服务
if ! command -v bluetoothctl >/dev/null 2>&1; then
    echo -e " [蓝牙] ${R}FAIL${N} (未安装 bluetoothctl)"
    exit 1
fi

# 检查 bluetoothd 进程是否在跑 (兼容非 systemd 嵌入式环境)
if ! pgrep -x bluetoothd >/dev/null 2>&1; then
    echo -e " [蓝牙] ${R}FAIL${N} (蓝牙服务 bluetoothd 未运行)"
    exit 1
fi

# 2. 获取蓝牙状态信息
BT_INFO=$(bluetoothctl show 2>/dev/null)

# 3. 检查核心条件：有 Controller (MAC) 且 Powered: yes
if echo "$BT_INFO" | grep -qE "Controller.*([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}"; then
    if echo "$BT_INFO" | grep -q "Powered: yes"; then
        # 提取 MAC 和 名称用于简要显示
        BT_MAC=$(echo "$BT_INFO" | awk '/Controller/{print $2}')
        BT_NAME=$(echo "$BT_INFO" | awk -F': ' '/Name:/{print $2}')
        echo -e " [蓝牙] ${G}OK${N} (已上电, MAC: $BT_MAC, 名称: $BT_NAME)"
    else
        echo -e " [蓝牙] ${R}FAIL${N} (硬件已识别，但未上电 Powered: no)"
    fi
else
    echo -e " [蓝牙] ${R}FAIL${N} (未检测到蓝牙控制器/缺驱动/硬件丢失)"
fi

echo "================================="
