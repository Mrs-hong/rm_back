#!/bin/bash
# 极简 Wi-Fi 能力验证脚本

# 自动获取无线接口名
IFACE=$(iw dev 2>/dev/null | awk '/Interface/{print $2; exit}')
[ -z "$IFACE" ] && IFACE="wlan0"

# 颜色定义：检测 stdout 是否为 TTY，非 TTY 时禁用颜色码避免 ANSI 序列污染管道输出
if [ -t 1 ]; then
    G='\e[32m'; R='\e[31m'; N='\e[0m'
else
    G=''; R=''; N=''
fi

echo "================================="
echo " Wi-Fi 极简验证 (接口: $IFACE)"
echo "================================="

# 1. 基础检查 (驱动、接口、未锁定)
BASE_OK=true
iw dev "$IFACE" info >/dev/null 2>&1 || BASE_OK=false
ip link show "$IFACE" 2>/dev/null | grep -q "UP" || ip link set "$IFACE" up 2>/dev/null || BASE_OK=false
rfkill list wifi 2>/dev/null | grep -q "blocked: yes" && BASE_OK=false

if $BASE_OK; then echo -e " [driver] ${G}OK${N} (驱动正常, 接口就绪)"
else echo -e " [基础] ${R}FAIL${N} (缺驱动/接口down/被rfkill锁定)"; fi

# 2. STA 模式检查 (支持managed, 能扫描, 有wpa_supplicant)
STA_OK=true
iw list 2>/dev/null | grep -A 15 "Supported interface modes" | grep -q "managed" || STA_OK=false
iw dev "$IFACE" scan >/dev/null 2>&1 || STA_OK=false # 即使没扫到，不报error即为通路正常
command -v wpa_supplicant >/dev/null 2>&1 || STA_OK=false

if $STA_OK; then echo -e " [STA]  ${G}OK${N} (支持客户端模式, 扫描正常, wpa_supplicant就绪)"
else echo -e " [STA]  ${R}FAIL${N} (不支持managed/扫描失败/缺wpa_supplicant)"; fi

# 3. AP 模式检查 (支持AP, 有hostapd, 检查进程状态防误判)
AP_OK=true
iw list 2>/dev/null | grep -A 15 "Supported interface modes" | grep -q "\* AP" || AP_OK=false
command -v hostapd >/dev/null 2>&1 || AP_OK=false

# 检查当前是否已经是AP模式，如果是，直接算OK，避免二次启动冲突误判
CURRENT_MODE=$(iw dev "$IFACE" info 2>/dev/null | awk '/type/{print $2}')
if [ "$CURRENT_MODE" != "AP" ]; then
    # 如果不是AP模式，尝试静默启动一下看能不能起来
    TEST_CONF=$(mktemp)
    cat > "$TEST_CONF" << EOF
interface=$IFACE
driver=nl80211
ssid=TestAP_$$
hw_mode=g
channel=6
EOF
    hostapd -B "$TEST_CONF" >/dev/null 2>&1
    sleep 1
    if pidof hostapd >/dev/null 2>&1; then
        killall hostapd >/dev/null 2>&1
    else
        AP_OK=false
    fi
    rm -f "$TEST_CONF"
fi

if $AP_OK; then echo -e " [AP]   ${G}OK${N} (支持热点模式, hostapd就绪/已运行)"
else echo -e " [AP]   ${R}FAIL${N} (不支持AP/缺hostapd/启动失败)"; fi

echo "================================="
