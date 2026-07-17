#!/bin/bash
# ==========================================================================
# SSD 读写压力检测脚本
#
# 基于 fio 测试 /data2（由 /dev/sda1 挂载）的读写功能。
#
# 测试内容：
#   1. 全 0 数据写入 10G，记录速度和耗时
#   2. 全 1 数据写入 10G，记录速度和耗时
#   3. 0101 交错数据写入 10G，记录速度和耗时
#   4. 顺序读取 10G，记录速度和耗时
#
# 判定规则：
#   所有步骤均成功执行为通过（exit 0）
#   任意步骤失败（含磁盘断开导致无法继续）为失败（exit 1）
#
# 输出协议：
#   每步输出 [PASS] 或 [FAIL] 关键字及速度/耗时信息，配合 PcbaChecker 解析
# ==========================================================================

set -o pipefail

# ===================== 配置区 =====================
MNT="/data2"                          # 挂载点（由 /dev/sda1 挂载）
FILE="$MNT/.fio_press_test"           # 测试文件（隐藏名，避免干扰）
BLOCK_DEV="/dev/sda1"                 # 对应块设备
SIZE="10G"                            # 每次测试数据量
BS="1M"                               # 块大小
IODEPTH=32                            # I/O 深度
RESULT=0                              # 整体结果：0=通过，1=存在失败

# ===================== 日志工具 =====================
log_info() { echo "[INFO] $*"; }
log_pass() { echo "[PASS] $*"; }
log_fail() { echo "[FAIL] $*"; }

# ===================== 清理测试文件 =====================
cleanup_file() {
    rm -f "$FILE" 2>/dev/null
    sync
}

# 脚本退出时兜底清理（含 Ctrl+C、SSH 断连等中断场景），
# 避免 10G 测试文件残留占用磁盘空间
trap cleanup_file EXIT

# ===================== 磁盘可用性检查 =====================
# 检查挂载点和块设备是否存在，磁盘断开时返回失败
check_disk() {
    if [ ! -b "$BLOCK_DEV" ]; then
        log_fail "块设备 $BLOCK_DEV 不存在"
        return 1
    fi
    if ! mountpoint -q "$MNT" 2>/dev/null; then
        log_fail "挂载点 $MNT 未挂载或不存在"
        return 1
    fi
    return 0
}

# ===================== 前置条件检查 =====================
check_prerequisites() {
    if ! command -v fio >/dev/null 2>&1; then
        log_fail "fio 未安装，无法执行 SSD 压力测试"
        return 1
    fi
    if ! check_disk; then
        return 1
    fi
    # 检查可用空间是否足够（至少 10G = 10240MB）
    local avail_mb
    avail_mb=$(df -m "$MNT" 2>/dev/null | awk 'NR==2{print $4}')
    if [ -z "$avail_mb" ] || [ "$avail_mb" -lt 10240 ] 2>/dev/null; then
        log_fail "$MNT 可用空间不足 10G（当前 ${avail_mb:-未知}MB）"
        return 1
    fi
    return 0
}

# ===================== 从 fio 输出提取速度 =====================
# fio normal 输出格式: "  write: IOPS=..., BW=1234MiB/s (1294MB/s), ..."
#                     "  read:  IOPS=..., BW=987MiB/s (1035MB/s), ..."
extract_bw() {
    local logfile="$1"
    local direction="$2"   # "write" 或 "read"
    grep -E "^\s+${direction}:" "$logfile" 2>/dev/null | \
        grep -oP 'BW=\K[0-9.]+[A-Za-z]+/s' | head -1
}

# ===================== fio 写入测试 =====================
# 参数：$1=pattern_hex(如 00/ff/55)  $2=desc(如 "全0数据")
run_write_test() {
    local pattern_hex="$1"
    local desc="$2"
    local logfile="/tmp/fio_press_write_${pattern_hex}.log"

    # 每步前检查磁盘是否仍然可用
    if ! check_disk; then
        log_fail "写入测试[$desc]: 磁盘不可用"
        return 1
    fi

    cleanup_file
    log_info ">>> 写入测试 [$desc] (pattern=0x${pattern_hex}, size=${SIZE})"

    local start_time end_time elapsed
    start_time=$(date +%s)

    if fio --name=press_write \
            --filename="$FILE" \
            --size="$SIZE" \
            --rw=write \
            --bs="$BS" \
            --direct=1 \
            --ioengine=libaio \
            --iodepth="$IODEPTH" \
            --numjobs=1 \
            --buffer_pattern="0x${pattern_hex}" \
            --group_reporting \
            > "$logfile" 2>&1; then
        end_time=$(date +%s)
        elapsed=$((end_time - start_time))
        local bw
        bw=$(extract_bw "$logfile" "write")
        log_pass "写入测试[$desc]: 速度=${bw:-未知} 耗时=${elapsed}s"
        cleanup_file
        return 0
    else
        log_fail "写入测试[$desc]: fio 执行失败"
        # 打印 fio 错误输出的最后几行，便于诊断
        tail -5 "$logfile" 2>/dev/null
        cleanup_file
        return 1
    fi
}

# ===================== fio 读取测试 =====================
run_read_test() {
    local logfile="/tmp/fio_press_read.log"
    local preplog="/tmp/fio_press_read_prep.log"

    if ! check_disk; then
        log_fail "读取测试: 磁盘不可用"
        return 1
    fi

    # 先写入 10G 测试文件用于后续读取
    log_info ">>> 准备读取测试文件 (${SIZE})"
    if ! fio --name=prep_read \
            --filename="$FILE" \
            --size="$SIZE" \
            --rw=write \
            --bs="$BS" \
            --direct=1 \
            --ioengine=libaio \
            --iodepth="$IODEPTH" \
            --numjobs=1 \
            --buffer_pattern=0xAA \
            --group_reporting \
            > "$preplog" 2>&1; then
        log_fail "读取测试: 准备测试文件失败"
        cleanup_file
        return 1
    fi

    # 再次确认磁盘可用后执行读取
    if ! check_disk; then
        log_fail "读取测试: 磁盘在准备阶段后断开"
        cleanup_file
        return 1
    fi

    log_info ">>> 读取测试 (顺序读, size=${SIZE})"

    local start_time end_time elapsed
    start_time=$(date +%s)

    if fio --name=press_read \
            --filename="$FILE" \
            --size="$SIZE" \
            --rw=read \
            --bs="$BS" \
            --direct=1 \
            --ioengine=libaio \
            --iodepth="$IODEPTH" \
            --numjobs=1 \
            --group_reporting \
            > "$logfile" 2>&1; then
        end_time=$(date +%s)
        elapsed=$((end_time - start_time))
        local bw
        bw=$(extract_bw "$logfile" "read")
        log_pass "读取测试: 速度=${bw:-未知} 耗时=${elapsed}s"
        cleanup_file
        return 0
    else
        log_fail "读取测试: fio 执行失败"
        tail -5 "$logfile" 2>/dev/null
        cleanup_file
        return 1
    fi
}

# ===================== 主流程 =====================
log_info "========== SSD 读写压力测试开始 =========="
log_info "挂载点: $MNT  块设备: $BLOCK_DEV  数据量: $SIZE"

# 前置检查
if ! check_prerequisites; then
    log_fail "前置条件检查失败，无法执行 SSD 压力测试"
    exit 1
fi

# 2.1 全 0 数据写入
run_write_test "00" "全0数据" || RESULT=1

# 2.2 全 1 数据写入
run_write_test "ff" "全1数据" || RESULT=1

# 2.3 0101 交错数据写入（0x55 = 二进制 01010101）
run_write_test "55" "0101交错" || RESULT=1

# 3. 顺序读取测试
run_read_test || RESULT=1

# 汇总结果
log_info "========== SSD 读写压力测试结束 =========="
if [ "$RESULT" -eq 0 ]; then
    log_pass "SSD 读写压力测试整体通过"
    exit 0
else
    log_fail "SSD 读写压力测试存在失败项"
    exit 1
fi
