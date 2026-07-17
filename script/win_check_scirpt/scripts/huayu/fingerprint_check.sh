#!/bin/bash
# ==========================================================================
# 指纹模组综合检测脚本
#
# 合并原 fingerprint_green.sh / fingerprint_red.sh / fingerprint_test.sh /
# fingerprint_close.sh 四个脚本，统一完成以下检测：
#   1. 心跳命令（03 03）→ 验证 22 字节响应
#   2. 开绿灯（0F 01 01）→ 验证 22 字节响应
#   3. 开红灯（0F 01 02）→ 验证 22 字节响应
#   4. 查询手指在位（01 35）→ 验证 23 字节响应
#   5. 关闭 LED（0F 00 00）→ 验证响应
#
# 退出码：0=通过，1=失败
# ==========================================================================

set -o pipefail

# ===================== 配置区 =====================
SERIAL_PORT="/dev/ttyACM1"
BAUDRATE="57600"
RESP_FILE="/tmp/fingerprint_check_resp.bin"

# 心跳期望响应（22 字节）
HEARTBEAT_CMD='\xF1\x1F\xE2\x2E\xB6\x6B\xA8\x8A\x00\x07\x86\x00\x00\x00\x00\x03\x03\xFA'
HEARTBEAT_EXPECTED_HEX="f11fe22eb66ba88a000b8200000000030300000000fa"
HEARTBEAT_RESP_LEN=22

# 绿灯命令（22 字节响应）
GREEN_CMD='\xF1\x1F\xE2\x2E\xB6\x6B\xA8\x8A\x00\x0C\x81\x00\x00\x00\x00\x02\x0F\x01\x01\x64\x00\x00\x89'
GREEN_RESP_LEN=22

# 红灯命令（22 字节响应）
RED_CMD='\xF1\x1F\xE2\x2E\xB6\x6B\xA8\x8A\x00\x0C\x81\x00\x00\x00\x00\x02\x0F\x01\x02\x64\x00\x00\x88'
RED_RESP_LEN=22

# 手指在位查询命令（23 字节响应）
FINGER_CMD='\xF1\x1F\xE2\x2E\xB6\x6B\xA8\x8A\x00\x07\x86\x00\x00\x00\x00\x01\x35\xCA'
FINGER_RESP_LEN=23

# 关闭 LED 命令（22 字节响应）
CLOSE_CMD='\xF1\x1F\xE2\x2E\xB6\x6B\xA8\x8A\x00\x0C\x81\x00\x00\x00\x00\x02\x0F\x00\x00\x00\x00\x00\xEF'
CLOSE_RESP_LEN=22

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

# 发送命令并接收响应
# 参数：$1=命令(printf格式)  $2=期望响应字节数  $3=是否校验hex(0=仅检查长度, 1=检查hex)  $4=期望hex(可选)
# 输出：接收到的字节数到 stdout
send_and_recv() {
    local cmd_bytes="$1"
    local expect_len="$2"
    local check_hex="$3"
    local expected_hex="$4"

    rm -f "$RESP_FILE"

    # 检查串口设备是否存在
    if [ ! -e "$SERIAL_PORT" ]; then
        echo "0"
        return 1
    fi

    # 设置串口参数
    if ! stty -F "$SERIAL_PORT" "$BAUDRATE" cs8 -parenb -cstopb \
        -ixon -ixoff -crtscts raw -echo 2>/dev/null; then
        echo "0"
        return 1
    fi

    # 清理串口中残留的旧数据
    timeout 0.2 dd if="$SERIAL_PORT" of=/dev/null bs=1 2>/dev/null

    # 启动接收进程
    timeout 3 dd if="$SERIAL_PORT" of="$RESP_FILE" bs=1 count="$expect_len" 2>/dev/null &
    local rx_pid=$!

    # 延时确保接收进程已就绪
    sleep 0.2

    # 发送命令
    printf "%b" "$cmd_bytes" > "$SERIAL_PORT"

    # 等待接收完成
    wait "$rx_pid" 2>/dev/null

    # 检查接收到的字节数
    local recv_len=0
    if [ -f "$RESP_FILE" ]; then
        recv_len=$(wc -c < "$RESP_FILE" 2>/dev/null || echo 0)
    fi

    # 字节数不足直接返回失败
    if [ "$recv_len" -lt "$expect_len" ]; then
        echo "$recv_len"
        return 1
    fi

    # 如果需要校验 hex
    if [ "$check_hex" = "1" ] && [ -n "$expected_hex" ]; then
        local recv_hex
        recv_hex=$(xxd -p "$RESP_FILE" 2>/dev/null | tr -d '\n\r ')
        if [ "$recv_hex" != "$expected_hex" ]; then
            echo "$recv_len"
            return 1
        fi
    fi

    echo "$recv_len"
    return 0
}

# ===================== 主流程 =====================
echo "================================="
echo " 指纹模组检测 (LED + 心跳 + 手指在位)"
echo "================================="

# 1. 心跳检测
echo ">>> 发送心跳命令..."
recv=$(send_and_recv "$HEARTBEAT_CMD" "$HEARTBEAT_RESP_LEN" 1 "$HEARTBEAT_EXPECTED_HEX")
rc=$?
if [ "$rc" -eq 0 ]; then
    report_item "心跳" 0 "(收到 ${recv} 字节响应)"
else
    report_item "心跳" 1 "(响应不足或 hex 不匹配, 收到 ${recv} 字节)"
fi

# 2. 开绿灯
echo ">>> 开绿灯..."
recv=$(send_and_recv "$GREEN_CMD" "$GREEN_RESP_LEN" 0 "")
rc=$?
if [ "$rc" -eq 0 ]; then
    report_item "绿灯" 0 "(LED 已点亮, 收到 ${recv} 字节响应)"
else
    report_item "绿灯" 1 "(响应不足, 收到 ${recv} 字节)"
fi

# 3. 开红灯
echo ">>> 开红灯..."
recv=$(send_and_recv "$RED_CMD" "$RED_RESP_LEN" 0 "")
rc=$?
if [ "$rc" -eq 0 ]; then
    report_item "红灯" 0 "(LED 已点亮, 收到 ${recv} 字节响应)"
else
    report_item "红灯" 1 "(响应不足, 收到 ${recv} 字节)"
fi

# 4. 查询手指在位
echo ">>> 查询手指在位..."
recv=$(send_and_recv "$FINGER_CMD" "$FINGER_RESP_LEN" 0 "")
rc=$?
if [ "$rc" -eq 0 ]; then
    report_item "手指" 0 "(查询响应正常, 收到 ${recv} 字节)"
else
    report_item "手指" 1 "(响应不足, 收到 ${recv} 字节)"
fi

# 5. 关闭 LED
echo ">>> 关闭 LED..."
recv=$(send_and_recv "$CLOSE_CMD" "$CLOSE_RESP_LEN" 0 "")
rc=$?
if [ "$rc" -eq 0 ]; then
    report_item "关闭" 0 "(LED 已关闭, 收到 ${recv} 字节响应)"
else
    report_item "关闭" 1 "(响应不足, 收到 ${recv} 字节)"
fi

# 清理临时文件
rm -f "$RESP_FILE"

# ===================== 汇总结果 =====================
echo "---------------------------------"
if [ "$OVERALL_PASS" -eq 0 ]; then
    echo -e " 结论: ${G}成功${N}，指纹模组功能正常"
    echo "================================="
    exit 0
else
    echo -e " 结论: ${R}失败${N}，异常项: ${FAIL_ITEMS}"
    echo "================================="
    exit 1
fi
