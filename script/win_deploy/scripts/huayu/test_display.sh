#!/bin/bash
# ==========================================================================
# TFT 屏幕通信检测脚本
#
# 通过串口向 TFT 屏幕发送控制指令，验证屏幕应答是否为 "OK"（5A A5 03 82 4F 4B）。
# 测试两个状态切换：唤醒（00 00）和待机（00 01），确保屏幕通信正常。
#
# 串口配置：/dev/ttyS2, 115200bps, 8N1, raw
# 唤醒指令：5A A5 07 82 00 84 5A 01 00 00（唤醒待机控制寄存器）
# 期望应答：5A A5 03 82 4F 4B（6 字节，ASCII "OK"）
#
# 退出码：0=通过，1=失败
# ==========================================================================

set -o pipefail

# ===================== 配置区 =====================
SERIAL_PORT="/dev/ttyS2"
BAUDRATE="115200"
RESP_FILE="/tmp/tft_display_resp.bin"

# 期望应答（6 字节 "OK" 响应）
EXPECTED_HEX="5aa503824f4b"
EXPECT_LEN=6

# 唤醒指令（最后一字节 00 = 唤醒）
WAKEUP_CMD='\x5A\xA5\x07\x82\x00\x84\x5A\x01\x00\x00'

# 待机指令（最后一字节 01 = 待机）
STANDBY_CMD='\x5A\xA5\x07\x82\x00\x84\x5A\x01\x00\x01'

# ===================== 颜色定义 =====================
# 检测 stdout 是否为 TTY：非 TTY（如 plink 管道/重定向）时禁用颜色码，
# 避免 ANSI 转义序列污染输出
if [ -t 1 ]; then
    G='\e[32m'; R='\e[31m'; Y='\e[33m'; N='\e[0m'
else
    G=''; R=''; Y=''; N=''
fi

# ===================== 结果统计 =====================
OVERALL_PASS=0
FAIL_ITEMS=""

# ===================== 工具函数 =====================

# 记录单项检测结果
# 参数：$1=检测项名称  $2=是否通过(0/1)  $3=说明
report_item() {
    local name="$1"
    local ok="$2"
    local desc="$3"
    if [ "$ok" -eq 0 ]; then
        echo -e " [${G}OK${N}]   ${name} ${desc}"
    else
        echo -e " [${R}FAIL${N}] ${name} ${desc}"
        OVERALL_PASS=1
        FAIL_ITEMS="${FAIL_ITEMS}${name}; "
    fi
}

# 发送指令并接收响应，校验 hex 是否匹配
# 参数：$1=指令(printf格式)  $2=检测项名称
# 返回：0=通过，1=失败
send_and_verify() {
    local cmd_bytes="$1"
    local name="$2"

    rm -f "$RESP_FILE"

    # 检查串口设备是否存在
    if [ ! -e "$SERIAL_PORT" ]; then
        report_item "$name" 1 "(串口设备不存在: $SERIAL_PORT)"
        return 1
    fi

    # 设置串口参数：115200, 8N1, raw
    if ! stty -F "$SERIAL_PORT" "$BAUDRATE" cs8 -parenb -cstopb \
        -ixon -ixoff -crtscts raw -echo 2>/dev/null; then
        report_item "$name" 1 "(串口参数设置失败)"
        return 1
    fi

    # 清理串口中残留的旧数据
    timeout 0.2 dd if="$SERIAL_PORT" of=/dev/null bs=1 2>/dev/null

    # 启动接收进程，最多等 3 秒，最多收 6 字节
    timeout 3 dd if="$SERIAL_PORT" of="$RESP_FILE" bs=1 count="$EXPECT_LEN" 2>/dev/null &
    local rx_pid=$!

    # 延时确保接收进程已就绪
    sleep 0.2

    # 发送指令
    printf "%b" "$cmd_bytes" > "$SERIAL_PORT"

    # 等待接收完成
    wait "$rx_pid" 2>/dev/null

    # 检查接收到的字节数
    local recv_len=0
    if [ -f "$RESP_FILE" ]; then
        recv_len=$(wc -c < "$RESP_FILE" 2>/dev/null || echo 0)
    fi

    # 字节数不足，直接失败
    if [ "$recv_len" -lt "$EXPECT_LEN" ]; then
        report_item "$name" 1 "(响应不足: 收到 ${recv_len} 字节, 期望 ${EXPECT_LEN} 字节)"
        return 1
    fi

    # 校验 hex 是否匹配
    local recv_hex
    recv_hex=$(xxd -p "$RESP_FILE" 2>/dev/null | tr -d '\n\r ')
    if [ "$recv_hex" = "$EXPECTED_HEX" ]; then
        report_item "$name" 0 "(收到 OK 响应, ${recv_len} 字节)"
        return 0
    else
        report_item "$name" 1 "(hex 不匹配: 收到 ${recv_hex}, 期望 ${EXPECTED_HEX})"
        return 1
    fi
}

# ===================== 主流程 =====================
echo "================================="
echo " TFT 屏幕通信检测 (唤醒 + 待机)"
echo "================================="

# 1. 唤醒测试
echo ">>> 发送唤醒指令..."
send_and_verify "$WAKEUP_CMD" "唤醒"

# 2. 待机测试
echo ">>> 发送待机指令..."
send_and_verify "$STANDBY_CMD" "待机"

# 清理临时文件
rm -f "$RESP_FILE"

# ===================== 汇总结果 =====================
echo "---------------------------------"
if [ "$OVERALL_PASS" -eq 0 ]; then
    echo -e " 结论: ${G}成功${N}，TFT 屏幕通信正常"
    echo "================================="
    exit 0
else
    echo -e " 结论: ${R}失败${N}，异常项: ${FAIL_ITEMS}"
    echo "================================="
    exit 1
fi
