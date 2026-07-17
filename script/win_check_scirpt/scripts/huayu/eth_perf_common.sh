#!/bin/bash

# BM1684X eth0 <-> eth1 网口性能通用测试脚本
# 接线方式：
#   eth0 <---- 网线 ----> eth1
#
# 用法：
#   eth_perf_common.sh MODE TEST_TIME PARALLEL MIN_MBPS INTERVAL
#
# 返回值：
#   0 = PASS
#   1 = FAIL
#   2 = 脚本条件错误

MODE="${1:-quick}"
TEST_TIME="${2:-10}"
PARALLEL="${3:-4}"
MIN_MBPS="${4:-800}"
INTERVAL="${5:-1}"

IF_A="${6:-eth0}"
IF_B="${7:-eth1}"

IP_A="${8:-192.168.112.47}"
IP_B="${9:-192.168.112.48}"
MASK="${10:-24}"

NS_NAME="ns_eth_perf_test"
MAX_RETR="${11:-0}"

RESULT=0
SERVER_PID=""

SPEED_A_TO_B=0
SPEED_B_TO_A=0
RETR_A_TO_B=0
RETR_B_TO_A=0

log()
{
    echo "[INFO] $*"
}

pass()
{
    echo "[PASS] $*"
}

fail()
{
    echo "[FAIL] $*"
    RESULT=1
}

cleanup()
{
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" >/dev/null 2>&1
    fi

    if ip netns list 2>/dev/null | awk '{print $1}' | grep -qx "$NS_NAME"; then
        ip netns exec "$NS_NAME" ip link set "$IF_B" down >/dev/null 2>&1
        ip netns exec "$NS_NAME" ip link set "$IF_B" netns 1 >/dev/null 2>&1
        ip netns del "$NS_NAME" >/dev/null 2>&1
    fi

    ip link set "$IF_A" up >/dev/null 2>&1
    ip link set "$IF_B" up >/dev/null 2>&1

    if command -v netplan >/dev/null 2>&1; then
        netplan apply >/dev/null 2>&1
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

get_speed()
{
    local logfile="$1"
    local rate

    rate=$(awk '/\[SUM\].*receiver/ {r=$(NF-2)} END {print r}' "$logfile")

    if [ -z "$rate" ]; then
        rate=$(awk '/receiver/ {r=$(NF-2)} END {print r}' "$logfile")
    fi

    echo "${rate:-0}"
}

get_retr()
{
    local logfile="$1"
    local retr

    retr=$(awk '/\[SUM\].*sender/ {r=$(NF-1)} END {print r}' "$logfile")

    if [ -z "$retr" ]; then
        retr=$(awk '/sender/ {r=$(NF-1)} END {print r}' "$logfile")
    fi

    echo "${retr:-0}"
}

check_speed()
{
    local name="$1"
    local rate="$2"

    awk -v r="$rate" -v t="$MIN_MBPS" 'BEGIN {exit !(r >= t)}'

    if [ $? -eq 0 ]; then
        pass "$name throughput ${rate} Mbits/sec >= ${MIN_MBPS} Mbits/sec"
    else
        fail "$name throughput ${rate} Mbits/sec < ${MIN_MBPS} Mbits/sec"
    fi
}

check_retr()
{
    local name="$1"
    local retr="$2"

    if [ "$retr" -le "$MAX_RETR" ] 2>/dev/null; then
        pass "$name TCP Retr ${retr}"
    else
        fail "$name TCP Retr ${retr} > ${MAX_RETR}"
    fi
}

read_stat_main()
{
    cat /sys/class/net/"$IF_A"/statistics/"$1" 2>/dev/null || echo 0
}

read_stat_ns()
{
    ip netns exec "$NS_NAME" cat /sys/class/net/"$IF_B"/statistics/"$1" 2>/dev/null || echo 0
}

check_delta_errors()
{
    local name="$1"
    local rx_err0="$2"
    local tx_err0="$3"
    local rx_drop0="$4"
    local tx_drop0="$5"
    local rx_over0="$6"
    local tx_carrier0="$7"
    local coll0="$8"

    local rx_err1="$9"
    local tx_err1="${10}"
    local rx_drop1="${11}"
    local tx_drop1="${12}"
    local rx_over1="${13}"
    local tx_carrier1="${14}"
    local coll1="${15}"

    local d_rx_err=$((rx_err1 - rx_err0))
    local d_tx_err=$((tx_err1 - tx_err0))
    local d_rx_drop=$((rx_drop1 - rx_drop0))
    local d_tx_drop=$((tx_drop1 - tx_drop0))
    local d_rx_over=$((rx_over1 - rx_over0))
    local d_tx_carrier=$((tx_carrier1 - tx_carrier0))
    local d_coll=$((coll1 - coll0))

    echo "$name delta:"
    echo "  rx_errors       : $d_rx_err"
    echo "  tx_errors       : $d_tx_err"
    echo "  rx_dropped      : $d_rx_drop"
    echo "  tx_dropped      : $d_tx_drop"
    echo "  rx_over_errors  : $d_rx_over"
    echo "  tx_carrier_err  : $d_tx_carrier"
    echo "  collisions      : $d_coll"

    if [ "$d_rx_err" -eq 0 ] && \
       [ "$d_tx_err" -eq 0 ] && \
       [ "$d_rx_over" -eq 0 ] && \
       [ "$d_tx_carrier" -eq 0 ] && \
       [ "$d_coll" -eq 0 ]; then
        pass "$name hardware error delta is 0"
    else
        fail "$name hardware error increased"
    fi
}

echo "========================================"
echo " BM1684X Ethernet Performance Test"
echo " Mode        : $MODE"
echo " Interface A : $IF_A"
echo " Interface B : $IF_B"
echo " Cable       : $IF_A <--> $IF_B"
echo " IP A        : $IP_A/$MASK"
echo " IP B        : $IP_B/$MASK"
echo " Time        : ${TEST_TIME}s per direction"
echo " Parallel    : $PARALLEL"
echo " Interval    : ${INTERVAL}s"
echo " Min Speed   : ${MIN_MBPS} Mbits/sec"
echo "========================================"

if [ "$(id -u)" -ne 0 ]; then
    echo "[ERROR] please run as root"
    exit 2
fi

for cmd in ip ping awk grep iperf3; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "[ERROR] command not found: $cmd"
        exit 2
    fi
done

trap cleanup EXIT

log "remove old namespace if exists"

if ip netns list | awk '{print $1}' | grep -qx "$NS_NAME"; then
    ip netns exec "$NS_NAME" ip link set "$IF_B" netns 1 >/dev/null 2>&1
    ip netns del "$NS_NAME" >/dev/null 2>&1
fi

log "check interfaces"

if ! ip link show dev "$IF_A" >/dev/null 2>&1; then
    echo "[ERROR] interface not found: $IF_A"
    exit 2
fi

if ! ip link show dev "$IF_B" >/dev/null 2>&1; then
    echo "[ERROR] interface not found: $IF_B"
    exit 2
fi

log "check $IF_A current IP"

if ip -4 addr show dev "$IF_A" | grep -q "$IP_A"; then
    pass "$IF_A has IP $IP_A"
else
    log "$IF_A does not have $IP_A, add it temporarily"
    ip addr add "$IP_A/$MASK" dev "$IF_A" >/dev/null 2>&1

    if ip -4 addr show dev "$IF_A" | grep -q "$IP_A"; then
        pass "$IF_A add IP $IP_A OK"
    else
        echo "[ERROR] $IF_A add IP $IP_A failed"
        exit 2
    fi
fi

ip link set "$IF_A" up

log "create namespace: $NS_NAME"

if ! ip netns add "$NS_NAME"; then
    echo "[ERROR] create namespace failed"
    exit 2
fi

log "move $IF_B into namespace"

if ! ip link set "$IF_B" netns "$NS_NAME"; then
    echo "[ERROR] move $IF_B to namespace failed"
    exit 2
fi

log "configure $IF_B in namespace"

ip netns exec "$NS_NAME" ip link set "$IF_B" down
ip netns exec "$NS_NAME" ip addr flush dev "$IF_B"
ip netns exec "$NS_NAME" ip addr add "$IP_B/$MASK" dev "$IF_B"
ip netns exec "$NS_NAME" ip link set "$IF_B" up
ip netns exec "$NS_NAME" ip link set lo up

sleep 2

log "check physical link"

if wait_link_main "$IF_A"; then
    pass "$IF_A physical link up"
else
    fail "$IF_A physical link down"
fi

if wait_link_ns "$NS_NAME" "$IF_B"; then
    pass "$IF_B physical link up"
else
    fail "$IF_B physical link down"
fi

echo
echo "========== $IF_A ethtool =========="
if command -v ethtool >/dev/null 2>&1; then
    ethtool "$IF_A" 2>/dev/null | grep -E "Speed|Duplex|Auto-negotiation|Link detected"
fi

echo
echo "========== $IF_B ethtool =========="
if command -v ethtool >/dev/null 2>&1; then
    ip netns exec "$NS_NAME" ethtool "$IF_B" 2>/dev/null | grep -E "Speed|Duplex|Auto-negotiation|Link detected"
fi

echo
log "ping test before iperf3"

ping -c 3 -W 1 -I "$IF_A" "$IP_B" >/tmp/eth_ping_a_to_b.log 2>&1
if [ $? -eq 0 ]; then
    pass "$IF_A -> $IF_B ping OK"
else
    fail "$IF_A -> $IF_B ping failed"
    cat /tmp/eth_ping_a_to_b.log
fi

ip netns exec "$NS_NAME" ping -c 3 -W 1 -I "$IF_B" "$IP_A" >/tmp/eth_ping_b_to_a.log 2>&1
if [ $? -eq 0 ]; then
    pass "$IF_B -> $IF_A ping OK"
else
    fail "$IF_B -> $IF_A ping failed"
    cat /tmp/eth_ping_b_to_a.log
fi

echo
log "record counters before iperf3"

A_RX_ERR0=$(read_stat_main rx_errors)
A_TX_ERR0=$(read_stat_main tx_errors)
A_RX_DROP0=$(read_stat_main rx_dropped)
A_TX_DROP0=$(read_stat_main tx_dropped)
A_RX_OVER0=$(read_stat_main rx_over_errors)
A_TX_CARRIER0=$(read_stat_main tx_carrier_errors)
A_COLL0=$(read_stat_main collisions)

B_RX_ERR0=$(read_stat_ns rx_errors)
B_TX_ERR0=$(read_stat_ns tx_errors)
B_RX_DROP0=$(read_stat_ns rx_dropped)
B_TX_DROP0=$(read_stat_ns tx_dropped)
B_RX_OVER0=$(read_stat_ns rx_over_errors)
B_TX_CARRIER0=$(read_stat_ns tx_carrier_errors)
B_COLL0=$(read_stat_ns collisions)

log "start iperf3 server in namespace"

ip netns exec "$NS_NAME" iperf3 -s -B "$IP_B" >/tmp/eth_iperf_server.log 2>&1 &
SERVER_PID=$!

sleep 2

if ! kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    echo "[ERROR] iperf3 server start failed"
    cat /tmp/eth_iperf_server.log
    exit 2
fi

echo
log "iperf3 test: $IF_A -> $IF_B"

iperf3 -c "$IP_B" -B "$IP_A" -t "$TEST_TIME" -i "$INTERVAL" -P "$PARALLEL" -f m >/tmp/eth_iperf_a_to_b.log 2>&1
IPERF_RC=$?
cat /tmp/eth_iperf_a_to_b.log

if [ "$IPERF_RC" -eq 0 ]; then
    SPEED_A_TO_B=$(get_speed /tmp/eth_iperf_a_to_b.log)
    RETR_A_TO_B=$(get_retr /tmp/eth_iperf_a_to_b.log)
    check_speed "$IF_A -> $IF_B" "$SPEED_A_TO_B"
    check_retr "$IF_A -> $IF_B" "$RETR_A_TO_B"
else
    fail "$IF_A -> $IF_B iperf3 failed"
fi

echo
log "iperf3 test: $IF_B -> $IF_A"

iperf3 -c "$IP_B" -B "$IP_A" -t "$TEST_TIME" -i "$INTERVAL" -P "$PARALLEL" -R -f m >/tmp/eth_iperf_b_to_a.log 2>&1
IPERF_RC=$?
cat /tmp/eth_iperf_b_to_a.log

if [ "$IPERF_RC" -eq 0 ]; then
    SPEED_B_TO_A=$(get_speed /tmp/eth_iperf_b_to_a.log)
    RETR_B_TO_A=$(get_retr /tmp/eth_iperf_b_to_a.log)
    check_speed "$IF_B -> $IF_A" "$SPEED_B_TO_A"
    check_retr "$IF_B -> $IF_A" "$RETR_B_TO_A"
else
    fail "$IF_B -> $IF_A iperf3 failed"
fi

echo
log "record counters after iperf3"

A_RX_ERR1=$(read_stat_main rx_errors)
A_TX_ERR1=$(read_stat_main tx_errors)
A_RX_DROP1=$(read_stat_main rx_dropped)
A_TX_DROP1=$(read_stat_main tx_dropped)
A_RX_OVER1=$(read_stat_main rx_over_errors)
A_TX_CARRIER1=$(read_stat_main tx_carrier_errors)
A_COLL1=$(read_stat_main collisions)

B_RX_ERR1=$(read_stat_ns rx_errors)
B_TX_ERR1=$(read_stat_ns tx_errors)
B_RX_DROP1=$(read_stat_ns rx_dropped)
B_TX_DROP1=$(read_stat_ns tx_dropped)
B_RX_OVER1=$(read_stat_ns rx_over_errors)
B_TX_CARRIER1=$(read_stat_ns tx_carrier_errors)
B_COLL1=$(read_stat_ns collisions)

echo
echo "========== counter delta =========="
check_delta_errors "$IF_A" \
    "$A_RX_ERR0" "$A_TX_ERR0" "$A_RX_DROP0" "$A_TX_DROP0" "$A_RX_OVER0" "$A_TX_CARRIER0" "$A_COLL0" \
    "$A_RX_ERR1" "$A_TX_ERR1" "$A_RX_DROP1" "$A_TX_DROP1" "$A_RX_OVER1" "$A_TX_CARRIER1" "$A_COLL1"

echo
check_delta_errors "$IF_B" \
    "$B_RX_ERR0" "$B_TX_ERR0" "$B_RX_DROP0" "$B_TX_DROP0" "$B_RX_OVER0" "$B_TX_CARRIER0" "$B_COLL0" \
    "$B_RX_ERR1" "$B_TX_ERR1" "$B_RX_DROP1" "$B_TX_DROP1" "$B_RX_OVER1" "$B_TX_CARRIER1" "$B_COLL1"

echo
echo "========================================"
if [ "$RESULT" -eq 0 ]; then
    echo "ETH_${MODE}_TEST_RESULT=PASS"
    echo "$IF_A -> $IF_B throughput: ${SPEED_A_TO_B} Mbits/sec"
    echo "$IF_B -> $IF_A throughput: ${SPEED_B_TO_A} Mbits/sec"
    echo "$IF_A -> $IF_B TCP Retr: ${RETR_A_TO_B}"
    echo "$IF_B -> $IF_A TCP Retr: ${RETR_B_TO_A}"
else
    echo "ETH_${MODE}_TEST_RESULT=FAIL"
    echo "$IF_A -> $IF_B throughput: ${SPEED_A_TO_B} Mbits/sec"
    echo "$IF_B -> $IF_A throughput: ${SPEED_B_TO_A} Mbits/sec"
    echo "$IF_A -> $IF_B TCP Retr: ${RETR_A_TO_B}"
    echo "$IF_B -> $IF_A TCP Retr: ${RETR_B_TO_A}"
fi
echo "========================================"

exit "$RESULT"
