#!/bin/bash
# PCBA 硬件自检汇总脚本
# 被 qifeng-scm 的 pcba checker 调用，用于整合板子提供商的硬件检测脚本。
# 脚本按退出码判定整体结果：0 表示全部通过，非 0 表示存在失败项。
#
# 使用方式：
#   1. 将板子提供商提供的硬件检测脚本放到设备端固定目录（如 /opt/qifeng-scm/vendor/）。
#   2. 在本脚本的 VENDOR_SCRIPTS 数组中填入这些脚本的绝对路径。
#   3. 也可在下方新增 check_xxx 内联函数实现自定义检测。
#   4. pcba checker 会通过 selftest.json:pcba.exe_command 调用本脚本。

set -o pipefail

# ===================== 配置区：厂家脚本列表 =====================
# TODO：根据实际厂家提供的脚本路径修改此数组。
# 每个脚本需要满足：可执行，退出码 0 表示通过，非 0 表示失败。
# 脚本输出中建议包含 PASS/FAIL 关键字，便于 pcba checker 做输出解析。
VENDOR_SCRIPTS=(
    # "/opt/qifeng-scm/vendor/check_disk.sh"
    # "/opt/qifeng-scm/vendor/check_fan.sh"
    # "/opt/qifeng-scm/vendor/check_display.sh"
    # "/opt/qifeng-scm/vendor/check_fingerprint.sh"
    # "/opt/qifeng-scm/vendor/check_microphone.sh"
)

# 厂家脚本存放目录，也可通过环境变量覆盖
VENDOR_DIR="${VENDOR_DIR:-/opt/qifeng-scm/vendor}"

# ===================== 日志与结果汇总工具 =====================
log_info() { echo "[INFO] $*"; }
log_pass() { echo "[PASS] $*"; }
log_warn() { echo "[WARN] $*"; }
log_fail() { echo "[FAIL] $*"; }

OVERALL_PASS=0
FAIL_ITEMS=""

report_result() {
    local name="$1"
    local rc="$2"
    if [ "$rc" -eq 0 ]; then
        log_pass "$name"
    else
        log_fail "$name"
        OVERALL_PASS=1
        FAIL_ITEMS="${FAIL_ITEMS}${name}; "
    fi
}

# ===================== 厂家脚本调用器 =====================
run_vendor_script() {
    local script_path="$1"
    local script_name
    script_name=$(basename "$script_path")

    if [ ! -f "$script_path" ]; then
        log_warn "厂家脚本不存在，跳过: $script_path"
        return 0
    fi

    if [ ! -x "$script_path" ]; then
        log_warn "厂家脚本不可执行，尝试自动添加执行权限: $script_path"
        chmod +x "$script_path" 2>/dev/null || true
    fi

    log_info "执行厂家脚本: $script_path"
    "$script_path"
    return $?
}

# ===================== 内联检测函数（可选/占位） =====================
# 若厂家未提供某个单项脚本，可在此补充实现；否则保持为空函数或删除。

check_external_disk() {
    log_info "开始外接磁盘读写压力测试..."
    # TODO：替换为实际检测命令或调用厂家脚本
    local target_disk="/dev/sda"
    if [ ! -b "$target_disk" ]; then
        echo "未找到外接磁盘 $target_disk"
        return 1
    fi
    echo "外接磁盘 $target_disk 存在"
    return 0
}

check_fan() {
    log_info "开始风扇检测..."
    # TODO：替换为实际检测命令或调用厂家脚本
    local found=0
    for hwmon in /sys/class/hwmon/hwmon*; do
        if [ -r "$hwmon/name" ]; then
            local name
            name=$(cat "$hwmon/name" 2>/dev/null)
            for fan_input in "$hwmon"/fan*_input; do
                if [ -r "$fan_input" ]; then
                    local rpm
                    rpm=$(cat "$fan_input" 2>/dev/null)
                    echo "风扇 $name: $rpm RPM"
                    found=1
                fi
            done
        fi
    done
    if [ "$found" -eq 0 ]; then
        echo "未找到风扇转速信息"
        return 1
    fi
    return 0
}

check_display() {
    log_info "开始显示器检测..."
    # TODO：替换为实际检测命令或调用厂家脚本
    if [ -d "/sys/class/drm" ]; then
        local connected=0
        for conn in /sys/class/drm/card*-*/status; do
            if [ -r "$conn" ]; then
                local status
                status=$(cat "$conn" 2>/dev/null)
                echo "显示器 $conn: $status"
                if [ "$status" = "connected" ]; then
                    connected=1
                fi
            fi
        done
        if [ "$connected" -eq 0 ]; then
            echo "未检测到已连接的显示器"
            return 1
        fi
        return 0
    fi
    echo "未找到 DRM 设备"
    return 1
}

check_fingerprint() {
    log_info "开始指纹模组检测..."
    # TODO：替换为实际检测命令或调用厂家脚本
    local fp_device="/dev/ttyS3"
    if [ ! -c "$fp_device" ]; then
        echo "未找到指纹模组设备 $fp_device"
        return 1
    fi
    echo "指纹模组设备 $fp_device 存在"
    return 0
}

check_microphone() {
    log_info "开始麦克风检测..."
    # TODO：替换为实际检测命令或调用厂家脚本
    if command -v arecord >/dev/null 2>&1; then
        local tmp_wav="/tmp/mic_test.wav"
        arecord -D default -d 2 -f S16_LE -r 16000 -c 1 "$tmp_wav" >/dev/null 2>&1
        if [ -s "$tmp_wav" ]; then
            echo "麦克风录音成功"
            rm -f "$tmp_wav"
            return 0
        else
            echo "麦克风录音失败"
            rm -f "$tmp_wav"
            return 1
        fi
    fi
    echo "未找到 arecord 工具"
    return 1
}

# ===================== 主流程 =====================
log_info "========== PCBA 硬件自检开始 =========="

# 1. 执行厂家提供的脚本列表
if [ ${#VENDOR_SCRIPTS[@]} -gt 0 ]; then
    log_info "开始执行厂家提供的硬件检测脚本..."
    for script in "${VENDOR_SCRIPTS[@]}"; do
        # 支持相对路径自动补全为 VENDOR_DIR 下的路径
        if [[ "$script" != /* ]]; then
            script="${VENDOR_DIR}/${script}"
        fi
        run_vendor_script "$script"
        report_result "$(basename "$script")" "$?"
    done
else
    log_warn "VENDOR_SCRIPTS 为空，将只执行内置占位检测项"
fi

# 2. 执行内联检测项（实际使用时可根据需要删除或启用）
log_info "开始执行内置占位检测项..."
check_external_disk
report_result "external_disk" "$?"

check_fan
report_result "fan" "$?"

check_display
report_result "display" "$?"

check_fingerprint
report_result "fingerprint" "$?"

check_microphone
report_result "microphone" "$?"

log_info "========== PCBA 硬件自检结束 =========="

if [ "$OVERALL_PASS" -eq 0 ]; then
    log_pass "PCBA 整体检测通过"
    exit 0
else
    log_fail "PCBA 整体检测未通过: $FAIL_ITEMS"
    exit 1
fi
