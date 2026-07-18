#!/bin/bash

# BM1684X eth0 <-> eth1 网口性能测试脚本（结果导向版）
# 接线方式：
#   eth0 <---- 一根网线 ----> eth1
#
# 测试内容：
#   1. Link 状态
#   2. eth0 -> eth1 TCP 吞吐
#   3. eth1 -> eth0 TCP 吞吐
#
# 输出策略：
#   终端仅显示 [PASS]/[FAIL] 单行结果 + 最终结论
#   详细日志（ethtool、iperf3 原始输出、ping 统计、counter）写入 /tmp/eth_perf_detail.log
#
# 返回值：
#   0 = PASS
#   1 = FAIL
#   2 = 脚本运行条件错误

IF_A="${1:-eth0}"
IF_B="${2:-eth1}"

IP_A="${3:-192.168.112.47}"
IP_B="${4:-192.168.112.48}"
MASK="${5:-24}"

NS_NAME="ns_eth_perf_test"

TEST_TIME=10
PARALLEL=4

# 千兆网口产线阈值 800 Mbits/sec
MIN_MBPS=800

# 详细日志文件（ethtool/iperf3 原始输出/ping/counter 等）
DETAIL_LOG="/tmp/eth_perf_detail.log"

RESULT=0
SERVER_PID=""

# ===================== 颜色定义 =====================
# 检测 stdout 是否为 TTY：非 TTY 时禁用颜色码，避免 ANSI 序列污染管道输出
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
        echo -e " [${G}PASS${N}] ${name} ${desc}"
    else
        echo -e " [${R}FAIL${N}] ${name} ${desc}"
        OVERALL_PASS=1
        FAIL_ITEMS="${FAIL_ITEMS}${name}; "
    fi
}

# 写入详细日志（不输出到终端）
log_detail() {
    echo "$*" >> "$DETAIL_LOG" 2>&1
}

cleanup()
{
    log_detail ""
    log_detail "=== cleanup and restore network ==="

    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" >/dev/null 2>&1
    fi

    if ip netns list | awk '{print $1}' | grep -qx "$NS_NAME"; then
        ip netns exec "$NS_NAME" ip link set "$IF_B" down >/dev/null 2>&1
        ip netns exec "$NS_NAME" ip link set "$IF_B" netns 1 >/dev/null 2>&1
        ip netns del "$NS_NAME" >/dev/null 2>&1
    fi

    ip link set "$IF_A" up >/dev/null 2>&1
    ip link set "$IF_B" up >/dev/null 2>&1

    if command -v netplan >/dev/null 2>&1; then
        netplan apply >/dev/null 2>&1
        log_detail "netplan apply done"
    fi
}

wait_link_main()
{
    local ifname="$1"
    local i

    for i in $(seq 1 10); do
        if cat /sys/class/net/"$ifname"/carrier 2>/dev/null | grep -q "1"; then
            return 0
        fi
        sleep 1
    done

    return 1
}

wait_link_ns()
{
    local ns="$1"
    local ifname="$2"
    local i

    for i in $(seq 1 10); do
        if ip netns exec "$ns" sh -c "cat /sys/class/net/$ifname/carrier 2>/dev/null" | grep -q "1"; then
            return 0
        fi
        sleep 1
    done

    return 1
}

# 从 iperf3 日志提取吞吐速率（Mbits/sec）
# 参数：$1=iperf3 日志文件路径
get_speed()
{
    local logfile="$1"
    local rate

    # 优先取 [SUM] receiver 行
    rate=$(awk '/\[SUM\].*receiver/ {r=$(NF-2)} END {print r}' "$logfile")

    if [ -z "$rate" ]; then
        rate=$(awk '/receiver/ {r=$(NF-2)} END {print r}' "$logfile")
    fi

    echo "${rate:-0}"
}

trap cleanup EXIT

# ===================== 主流程 =====================

# 初始化详细日志
echo "=== ETH Performance Test Detail Log ===" > "$DETAIL_LOG"
log_detail "IF_A=$IF_A IF_B=$IF_B IP_A=$IP_A IP_B=$IP_B MASK=$MASK"
log_detail "TEST_TIME=${TEST_TIME}s PARALLEL=$PARALLEL MIN_MBPS=$MIN_MBPS"

echo "================================="
echo " ETH 网口性能测试 (${IF_A} ↔ ${IF_B})"
echo "================================="

# 运行条件检查
if [ "$(id -u)" -ne 0 ]; then
    echo -e " [${R}FAIL${N}] 请使用 root 权限执行"
    exit 2
fi

for cmd in ip ping awk grep iperf3; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo -e " [${R}FAIL${N}] 缺少命令: $cmd"
        exit 2
    fi
done

# 检查接口是否存在
if ! ip link show dev "$IF_A" >/dev/null 2>&1; then
    echo -e " [${R}FAIL${N}] 接口不存在: $IF_A"
    exit 2
fi
if ! ip link show dev "$IF_B" >/dev/null 2>&1; then
    echo -e " [${R}FAIL${N}] 接口不存在: $IF_B"
    exit 2
fi

log_detail "--- check $IF_A current IP ---"
if ip -4 addr show dev "$IF_A" | grep -q "$IP_A"; then
    log_detail "$IF_A already has IP $IP_A"
else
    log_detail "$IF_A does not have $IP_A, add it temporarily"
    ip addr add "$IP_A/$MASK" dev "$IF_A" >/dev/null 2>&1
    if ! ip -4 addr show dev "$IF_A" | grep -q "$IP_A"; then
        echo -e " [${R}FAIL${N}] $IF_A 添加 IP $IP_A 失败"
        exit 2
    fi
fi

ip link set "$IF_A" up

# 清理旧 namespace
log_detail "--- remove old namespace if exists ---"
if ip netns list | awk '{print $1}' | grep -qx "$NS_NAME"; then
    ip netns exec "$NS_NAME" ip link set "$IF_B" netns 1 >/dev/null 2>&1
    ip netns del "$NS_NAME" >/dev/null 2>&1
fi

# 创建 namespace 并配置 eth1
log_detail "--- create namespace: $NS_NAME ---"
if ! ip netns add "$NS_NAME"; then
    echo -e " [${R}FAIL${N}] 创建 namespace 失败"
    exit 2
fi

log_detail "--- move $IF_B into namespace ---"
if ! ip link set "$IF_B" netns "$NS_NAME"; then
    echo -e " [${R}FAIL${N}] 移动 $IF_B 到 namespace 失败"
    exit 2
fi

log_detail "--- configure $IF_B in namespace ---"
ip netns exec "$NS_NAME" ip link set "$IF_B" down
ip netns exec "$NS_NAME" ip addr flush dev "$IF_B"
ip netns exec "$NS_NAME" ip addr add "$IP_B/$MASK" dev "$IF_B"
ip netns exec "$NS_NAME" ip link set "$IF_B" up
ip netns exec "$NS_NAME" ip link set lo up

sleep 2

# 1. 物理链路检测
if wait_link_main "$IF_A"; then
    report_item "${IF_A} 物理链路 up" 0 ""
else
    report_item "${IF_A} 物理链路 up" 1 "(链路未就绪)"
fi

if wait_link_ns "$NS_NAME" "$IF_B"; then
    report_item "${IF_B} 物理链路 up" 0 ""
else
    report_item "${IF_B} 物理链路 up" 1 "(链路未就绪)"
fi

# 记录 ethtool 信息到详细日志（不在终端显示）
log_detail ""
log_detail "========== $IF_A ethtool =========="
if command -v ethtool >/dev/null 2>&1; then
    ethtool "$IF_A" 2>/dev/null | grep -E "Speed|Duplex|Auto-negotiation|Link detected" >> "$DETAIL_LOG" 2>&1
fi

log_detail ""
log_detail "========== $IF_B ethtool =========="
if command -v ethtool >/dev/null 2>&1; then
    ip netns exec "$NS_NAME" ethtool "$IF_B" 2>/dev/null | grep -E "Speed|Duplex|Auto-negotiation|Link detected" >> "$DETAIL_LOG" 2>&1
fi

# 启动 iperf3 服务端（日志写入详细日志文件）
log_detail ""
log_detail "--- start iperf3 server in namespace ---"
ip netns exec "$NS_NAME" iperf3 -s -B "$IP_B" >/tmp/eth_iperf_server.log 2>&1 &
SERVER_PID=$!

sleep 2

if ! kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    log_detail "[ERROR] iperf3 server start failed"
    cat /tmp/eth_iperf_server.log >> "$DETAIL_LOG" 2>&1
    report_item "iperf3 服务端启动" 1 "(启动失败)"
    echo "---------------------------------"
    if [ "$OVERALL_PASS" -eq 0 ]; then
        echo -e " 结论: ${G}成功${N}，网口性能测试通过"
    else
        echo -e " 结论: ${R}失败${N}，异常项: ${FAIL_ITEMS}"
    fi
    echo "================================="
    exit "$OVERALL_PASS"
fi

# 2. eth0 -> eth1 吞吐测试
log_detail ""
log_detail "--- iperf3 test: $IF_A -> $IF_B ---"
iperf3 -c "$IP_B" -B "$IP_A" -t "$TEST_TIME" -P "$PARALLEL" -f m >/tmp/eth_iperf_a_to_b.log 2>&1
iperf3_rc_a_to_b=$?
log_detail "iperf3 rc=$iperf3_rc_a_to_b"
cat /tmp/eth_iperf_a_to_b.log >> "$DETAIL_LOG" 2>&1

if [ "$iperf3_rc_a_to_b" -eq 0 ]; then
    SPEED_A_TO_B=$(get_speed /tmp/eth_iperf_a_to_b.log)
    # 速率达标判定
    if awk -v r="$SPEED_A_TO_B" -v t="$MIN_MBPS" 'BEGIN {exit !(r >= t)}'; then
        report_item "${IF_A} -> ${IF_B} 吞吐" 0 "(${SPEED_A_TO_B} Mbits/sec >= ${MIN_MBPS})"
    else
        report_item "${IF_A} -> ${IF_B} 吞吐" 1 "(${SPEED_A_TO_B} Mbits/sec < ${MIN_MBPS})"
    fi
else
    SPEED_A_TO_B=0
    report_item "${IF_A} -> ${IF_B} 吞吐" 1 "(iperf3 连接失败)"
fi

# 3. eth1 -> eth0 吞吐测试
log_detail ""
log_detail "--- iperf3 test: $IF_B -> $IF_A ---"
iperf3 -c "$IP_B" -B "$IP_A" -t "$TEST_TIME" -P "$PARALLEL" -R -f m >/tmp/eth_iperf_b_to_a.log 2>&1
iperf3_rc_b_to_a=$?
log_detail "iperf3 rc=$iperf3_rc_b_to_a"
cat /tmp/eth_iperf_b_to_a.log >> "$DETAIL_LOG" 2>&1

if [ "$iperf3_rc_b_to_a" -eq 0 ]; then
    SPEED_B_TO_A=$(get_speed /tmp/eth_iperf_b_to_a.log)
    if awk -v r="$SPEED_B_TO_A" -v t="$MIN_MBPS" 'BEGIN {exit !(r >= t)}'; then
        report_item "${IF_B} -> ${IF_A} 吞吐" 0 "(${SPEED_B_TO_A} Mbits/sec >= ${MIN_MBPS})"
    else
        report_item "${IF_B} -> ${IF_A} 吞吐" 1 "(${SPEED_B_TO_A} Mbits/sec < ${MIN_MBPS})"
    fi
else
    SPEED_B_TO_A=0
    report_item "${IF_B} -> ${IF_A} 吞吐" 1 "(iperf3 连接失败)"
fi

# 记录 counter 信息到详细日志
log_detail ""
log_detail "========== $IF_A counters =========="
ip -s link show dev "$IF_A" >> "$DETAIL_LOG" 2>&1

log_detail ""
log_detail "========== $IF_B counters =========="
ip netns exec "$NS_NAME" ip -s link show dev "$IF_B" >> "$DETAIL_LOG" 2>&1

# ===================== 汇总结果 =====================
echo "---------------------------------"
if [ "$OVERALL_PASS" -eq 0 ]; then
    echo -e " 结论: ${G}成功${N}，网口性能测试通过"
    echo "       ${IF_A} -> ${IF_B}: ${SPEED_A_TO_B} Mbits/sec"
    echo "       ${IF_B} -> ${IF_A}: ${SPEED_B_TO_A} Mbits/sec"
else
    echo -e " 结论: ${R}失败${N}，异常项: ${FAIL_ITEMS}"
    echo "       ${IF_A} -> ${IF_B}: ${SPEED_A_TO_B:-0} Mbits/sec"
    echo "       ${IF_B} -> ${IF_A}: ${SPEED_B_TO_A:-0} Mbits/sec"
    echo "       (详细日志: $DETAIL_LOG)"
fi
echo "================================="

exit "$OVERALL_PASS"
