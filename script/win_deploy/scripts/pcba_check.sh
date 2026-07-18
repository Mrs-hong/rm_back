#!/bin/bash
# ==========================================================================
# PCBA 硬件自检 + SCM 安装与自检 统一编排脚本
#
# 本脚本是设备端的唯一编排器，统一管理以下流程：
#   阶段 1：厂商硬件自检（调用 scripts/<vendor>/ 下的检测脚本）
#   阶段 2：SSD 读写压力测试（press_check_ssd.sh）
#   阶段 3：SCM deb 包完整性校验（sha256）
#   阶段 4：SCM 前置依赖检查（check_scm_deps.sh）
#   阶段 5：安装 SCM（dpkg -i + apt-get install -f）
#   阶段 6：等待 qf_scmd 服务就绪
#   阶段 7：执行 SCM 自检（qf_scmc check --config）
#   阶段 8：解析自检报告并输出摘要
#
# 用法：
#   pcba_check.sh [--vendor huayu] [--testcheck-dir /data/testcheck]
#
# 需 root 权限执行（用于 dpkg 安装），Windows 脚本通过 sudo -S 调用。
#
# 退出码：0=全部通过，1=存在失败项
# ==========================================================================

set -o pipefail

# ===================== 配置区 =====================
VENDOR="huayu"
TESTCHECK_DIR="/data/testcheck"
REPORT_PATH="/var/log/qifeng-scm/selftest-report.json"
SERVICE_READY_TIMEOUT=10
SERVICE_READY_INTERVAL=1
# 单脚本执行超时（秒）— run.bat 通过 --script-timeout 传入；超时则 SIGTERM
SINGLE_SCRIPT_TIMEOUT=120

# phase 7 捕获的 qf_scmc check 终端输出路径（供 phase 8 解析，不再读 JSON 文件）
SCM_SELFTEST_LOG=""

# 本脚本所在目录（用于定位同目录的脚本和厂商子目录）
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ===================== 颜色定义（抽取到 _colors.sh） =====================
# 各脚本统一 source _colors.sh，避免颜色码定义散落
# shellcheck source=_colors.sh
source "$(dirname "${BASH_SOURCE[0]}")/_colors.sh"

# ===================== 结果统计 =====================
OVERALL_PASS=0
FAIL_ITEMS=""
declare -A ITEM_STATUS=()     # script_name -> PASS/FAIL
declare -A ITEM_REASON=()     # script_name -> error reason
TOTAL_COUNT=0
PASS_COUNT=0
FAIL_COUNT=0

# ===================== 清理陷阱 =====================
# EXIT trap: 脚本无论正常结束还是被中断（Ctrl+C/SSH 断连 SIGHUP/SIGTERM）
# 都会触发清理。注意：
#   - 本脚本通过 sudo -S 以 root 运行，有权限删除所有文件。
#   - 清理范围只限 testcheck 目录和脚本自身产生的 /tmp 日志，
#     不卸载已安装的 SCM/依赖包。
#   - bat 端同时维护"上传前清理"逻辑，双重保障。
cleanup_on_exit() {
    rm -rf "${TESTCHECK_DIR}" \
           /data/check \
           /tmp/.sudo_pwd \
           /tmp/scm_selftest_output.log \
           /tmp/scm_install.log \
           /tmp/offline_install.log \
           /tmp/eth_perf_detail.log \
           /tmp/fio_press_write_*.log \
           /tmp/fio_press_read*.log \
           2>/dev/null
    # 注意：不删除 /tmp/pcba_check_output.log，因为 bat 端需要下载此文件后
    # 才能获取完整的测试输出。该文件会在下次运行的开头的 cleanup 步骤中被清理。
}
trap cleanup_on_exit EXIT

# ===================== 参数解析 =====================
parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --vendor)
                VENDOR="$2"
                shift 2
                ;;
            --testcheck-dir)
                TESTCHECK_DIR="$2"
                shift 2
                ;;
            --script-timeout)
                SINGLE_SCRIPT_TIMEOUT="$2"
                shift 2
                ;;
            -h|--help)
                echo "用法: pcba_check.sh [--vendor huayu] [--testcheck-dir /data/testcheck] [--script-timeout 120]"
                exit 0
                ;;
            *)
                echo "[WARN] 未知参数: $1"
                shift
                ;;
        esac
    done
}

# ===================== 工具函数 =====================

# 脚本名 → 检查项中文名映射（用于失败总结时显示"检查项-脚本名"）
# builtin 项：""→"SCM 安装"等非脚本类检查
declare -A ITEM_LABEL=(
    ["test_serial_check.sh"]="串口屏指纹"
    ["test_display.sh"]="TFT屏幕"
    ["fingerprint_check.sh"]="指纹模组"
    ["test_eth_perf.sh"]="网口性能"
    ["test_temp_fan.sh"]="温度风扇"
    ["test_usb_useful.sh"]="USB存储"
    ["test_wifi.sh"]="WiFi"
    ["test_bluework.sh"]="蓝牙"
    ["press_check_ssd.sh"]="SSD压力测试"
    ["check_scm_deps.sh"]="SCM依赖"
    # builtin 检查项（由 report_result 直接传中文名）
    ["SCM安装"]="SCM安装"
    ["服务启动"]="服务启动"
    ["SCM 自检"]="SCM自检"
    ["模型推理测试"]="模型推理测试"
)

# 获取检查项的显示标签：
#   - 外部脚本项（如 test_serial_check.sh → "串口屏指纹-test_serial_check.sh"）
#   - 内置项（如 "SCM安装" → "SCM安装"）
get_item_label() {
    local script_name="$1"
    local label="${ITEM_LABEL[$script_name]}"
    if [ -n "$label" ]; then
        if [ "$label" = "$script_name" ]; then
            echo "$label"
        else
            echo "${label}-${script_name}"
        fi
    else
        echo "$script_name"
    fi
}

# 记录单项检测结果
# 参数：$1=检测项名称(脚本名或中文描述)  $2=退出码(0=通过)  $3=失败原因(可选)
report_result() {
    local name="$1"
    local rc="$2"
    local reason="${3:-}"
    local label
    label=$(get_item_label "$name")
    TOTAL_COUNT=$((TOTAL_COUNT + 1))
    if [ "$rc" -eq 0 ]; then
        echo -e " [${G}PASS${N}] $name"
        PASS_COUNT=$((PASS_COUNT + 1))
        ITEM_STATUS["$name"]="PASS"
        ITEM_REASON["$name"]=""
    else
        echo -e " [${R}FAIL${N}] $name"
        OVERALL_PASS=1
        FAIL_COUNT=$((FAIL_COUNT + 1))
        ITEM_STATUS["$name"]="FAIL"
        ITEM_REASON["$name"]="$reason"
        FAIL_ITEMS="${FAIL_ITEMS}${label}; "
    fi
}

# 从脚本输出中提取失败原因
# 参数：$1=日志文件路径  $2=退出码
extract_failure_reason() {
    local logfile="$1"
    local rc="$2"
    if [ ! -f "$logfile" ] || [ ! -s "$logfile" ]; then
        echo "执行失败(exit=${rc})"
        return
    fi
    # 取最后一行含 failed/error/FAIL/ERROR/失败/错误 的行
    local reason
    reason=$(grep -i "failed\|error\|FAIL\|ERROR\|失败\|错误" "$logfile" | tail -1)
    if [ -n "$reason" ]; then
        echo "$reason"
        return
    fi
    # 兜底：取最后一行非空行
    reason=$(awk '/^[[:space:]]*$/ {next} {last=$0} END {print last}' "$logfile")
    if [ -n "$reason" ]; then
        echo "$reason"
    else
        echo "执行失败(exit=${rc})"
    fi
}

# 执行外部脚本并报告结果（捕获输出到临时文件以提取错误原因）
# 参数：$1=脚本路径
run_script() {
    local script_path="$1"
    local script_name
    script_name=$(basename "$script_path")

    # 脚本不存在时跳过
    if [ ! -f "$script_path" ]; then
        echo -e " [${Y}SKIP${N}] ${script_name} (脚本不存在)"
        return 0
    fi

    # 自动补充可执行权限
    if [ ! -x "$script_path" ]; then
        chmod +x "$script_path" 2>/dev/null || true
    fi

    echo "---------------------------------"
    echo " 执行: ${script_name}"
    echo "---------------------------------"
    local tmp_log="/tmp/${script_name}.log"
    # 用 timeout 包装，避免单脚本卡死（如串口 read 阻塞、iperf3 不退出）
    # 导致整个 pcba_check.sh 流程永久挂起
    timeout "${SINGLE_SCRIPT_TIMEOUT}" "$script_path" 2>&1 | tee "$tmp_log"
    local rc=$?
    local reason=""
    if [ "$rc" -eq 124 ]; then
        reason="执行超时(${SINGLE_SCRIPT_TIMEOUT}s 被强制终止)"
    elif [ "$rc" -ne 0 ]; then
        reason=$(extract_failure_reason "$tmp_log" "$rc")
    fi
    report_result "$script_name" "$rc" "$reason"
    return "$rc"
}

# ===================== 阶段函数 =====================

# 阶段 1：厂商硬件自检
phase1_hardware_check() {
    local vendor_dir="${SCRIPT_DIR}/${VENDOR}"
    echo "================================="
    echo " 阶段 1: 硬件自检 (厂商: ${VENDOR})"
    echo "================================="

    if [ ! -d "$vendor_dir" ]; then
        echo -e " [${R}FAIL${N}] 厂商脚本目录不存在: $vendor_dir"
        OVERALL_PASS=1
        FAIL_ITEMS="${FAIL_ITEMS}厂商脚本目录; "
        return 1
    fi

    # 厂商硬件检测脚本执行顺序
    local vendor_scripts=(
        "test_serial_check.sh"      # MCU版本(ttyS1) + TFT屏(ttyS2) + 指纹心跳(ttyACM1)
        "test_display.sh"           # TFT 屏幕唤醒/待机状态切换验证
        "fingerprint_check.sh"      # 指纹 LED 变化 + 手指在位 + 关闭
        "test_eth_perf.sh"          # eth0↔eth1 网口性能测试（自包含，结果导向输出）
        "test_temp_fan.sh"          # 温度 + 风扇状态
        "test_usb_useful.sh"        # USB 存储可用性
        "test_wifi.sh"              # WiFi 能力验证
        "test_bluework.sh"          # 蓝牙可用性验证
    )

    for script in "${vendor_scripts[@]}"; do
        run_script "${vendor_dir}/${script}"
    done
}

# 阶段 2：SSD 读写压力测试
phase2_ssd_pressure() {
    echo "================================="
    echo " 阶段 2: SSD 读写压力测试"
    echo "================================="
    run_script "${SCRIPT_DIR}/press_check_ssd.sh"
}

# 阶段 3：SCM deb 包完整性校验
phase3_verify_deb() {
    echo "================================="
    echo " 阶段 3: SCM 包完整性校验"
    echo "================================="

    # 查找 deb 包和 sha256 文件
    # 注意：文件名可能是 qifeng-scm_*.deb（下划线，deb构建默认）或 qifeng-scm-*.deb（连字符）
    # 使用 qifeng-scm*.deb 通配以兼容两种命名
    local deb_file
    deb_file=$(ls "${TESTCHECK_DIR}"/qifeng-scm*.deb 2>/dev/null | head -1)
    if [ -z "$deb_file" ]; then
        echo -e " [${R}FAIL${N}] 未找到 SCM deb 包"
        OVERALL_PASS=1
        FAIL_ITEMS="${FAIL_ITEMS}deb未找到; "
        return 1
    fi
    echo " [INFO] deb 包: $(basename "$deb_file")"

    # 查找 sha256 文件
    local sha256_file="${deb_file}.sha256"
    if [ ! -f "$sha256_file" ]; then
        # 尝试在 testcheck_dir 下查找独立的 sha256 文件
        sha256_file=$(ls "${TESTCHECK_DIR}"/qifeng-scm*.deb.sha256 2>/dev/null | head -1)
    fi

    if [ -z "$sha256_file" ] || [ ! -f "$sha256_file" ]; then
        echo -e " [${R}FAIL${N}] 未找到 SHA256 校验文件"
        OVERALL_PASS=1
        FAIL_ITEMS="${FAIL_ITEMS}sha256文件缺失; "
        return 1
    fi
    echo " [INFO] SHA256 文件: $(basename "$sha256_file")"

    # 执行 sha256 校验
    # sha256 文件格式：<hash>  <filename>，需在文件所在目录执行
    local sha256_dir
    sha256_dir=$(dirname "$sha256_file")
    local sha256_base
    sha256_base=$(basename "$sha256_file")

    if (cd "$sha256_dir" && sha256sum -c "$sha256_base" >/dev/null 2>&1); then
        echo -e " [${G}PASS${N}] SHA256 校验通过"
    else
        echo -e " [${R}FAIL${N}] SHA256 校验失败，deb 包可能损坏"
        OVERALL_PASS=1
        FAIL_ITEMS="${FAIL_ITEMS}sha256校验失败; "
        return 1
    fi

    # 保存 deb 路径供后续阶段使用
    SCM_DEB_PATH="$deb_file"
}

# 阶段 4：SCM 前置依赖检查

# 检查失败时若存在离线依赖包（offline-debs/install_offline.sh），
# 自动执行离线安装后复检；无离线包则由安装阶段 apt-get install -f 兜底。
phase4_check_deps() {
    echo "================================="
    echo " 阶段 4: SCM 前置依赖检查"
    echo "================================="

    local deps_script="${SCRIPT_DIR}/check_scm_deps.sh"
    if [ ! -f "$deps_script" ]; then
        echo -e " [${Y}SKIP${N}] check_scm_deps.sh (脚本不存在)"
        return 0
    fi

    # 首次检查（直接执行显示输出，暂不记录结果，留待最终判定）
    echo "---------------------------------"
    echo " 执行: check_scm_deps.sh"
    echo "---------------------------------"
    if timeout "${SINGLE_SCRIPT_TIMEOUT}" bash "$deps_script"; then
        report_result "check_scm_deps.sh" 0
        return 0
    fi

    # 依赖缺失：尝试离线安装
    local offline_installer="${TESTCHECK_DIR}/offline-debs/install_offline.sh"
    if [ -f "$offline_installer" ]; then
        echo " [INFO] 依赖缺失，执行离线安装 (过程静默，详情见 /tmp/offline_install.log)..."
        if bash "$offline_installer" >/tmp/offline_install.log 2>&1; then
            echo -e " [${G}PASS${N}] 离线依赖安装完成，复检..."
        else
            echo -e " [${Y}WARN${N}] 离线安装执行失败 (详情: /tmp/offline_install.log)"
        fi
        # 复检并记录最终结果
        run_script "$deps_script"
    else
        echo -e " [${Y}WARN${N}] 未找到离线依赖包 (offline-debs)，由安装阶段 apt-get install -f 兜底"
        report_result "check_scm_deps.sh" 1 "离线依赖包不存在"
    fi
}

# 阶段 5：安装 SCM
phase5_install_scm() {
    echo "================================="
    echo " 阶段 5: 安装 SCM"
    echo "================================="

    if [ -z "$SCM_DEB_PATH" ] || [ ! -f "$SCM_DEB_PATH" ]; then
        echo -e " [${R}FAIL${N}] deb 包路径无效，无法安装"
        report_result "SCM安装" 1 "deb包路径无效"
        return 1
    fi

    # 直接 dpkg -i 覆盖安装，不检查是否已安装
    # 安装过程输出重定向到日志（含 ldconfig 警告等噪音），不在终端显示
    echo " [INFO] 安装: $(basename "$SCM_DEB_PATH") (过程静默，详情见 /tmp/scm_install.log)"
    if dpkg -i "$SCM_DEB_PATH" >/tmp/scm_install.log 2>&1 && \
       apt-get install -f -y >>/tmp/scm_install.log 2>&1; then
        echo -e " [${G}PASS${N}] SCM 安装成功"
        report_result "SCM安装" 0
    else
        echo -e " [${R}FAIL${N}] SCM 安装失败 (详情: /tmp/scm_install.log)"
        report_result "SCM安装" 1 "dpkg安装失败"
        return 1
    fi
}

# 阶段 6：等待 qf_scmd 服务就绪
phase6_wait_service() {
    echo "================================="
    echo " 阶段 6: 等待 qf_scmd 服务就绪"
    echo "================================="

    local elapsed=0
    while [ "$elapsed" -lt "$SERVICE_READY_TIMEOUT" ]; do
        # qf_scmc list 返回 [OK] 表示 UDS 可通信
        if qf_scmc list 2>/dev/null | grep -q "\[OK\]"; then
            echo -e " [${G}PASS${N}] qf_scmd 服务已就绪 (${elapsed}s)"
            report_result "服务启动" 0
            return 0
        fi
        sleep "$SERVICE_READY_INTERVAL"
        elapsed=$((elapsed + SERVICE_READY_INTERVAL))
    done

    echo -e " [${R}FAIL${N}] 等待 qf_scmd 服务超时 (${SERVICE_READY_TIMEOUT}s)"
    report_result "服务启动" 1 "超时(${SERVICE_READY_TIMEOUT}s)"
    return 1
}

# 阶段 7：执行 SCM 自检
phase7_scm_selftest() {
    echo "================================="
    echo " 阶段 7: SCM 自检"
    echo "================================="

    local config_path="${TESTCHECK_DIR}/config/selftest.json"
    if [ ! -f "$config_path" ]; then
        echo -e " [${R}FAIL${N}] 自检配置文件不存在: $config_path"
        report_result "SCM 自检" 1 "配置文件不存在"
        return 1
    fi

    echo " [INFO] 配置文件: $config_path"
    echo "---------------------------------"

    # 执行自检，输出重定向到日志文件（不在终端显示原始 JSON 格式）
    # 由 phase 8 统一解析后转换为标准检查项格式输出
    local selftest_log="/tmp/scm_selftest_output.log"
    qf_scmc check --config "$config_path" > "$selftest_log" 2>&1
    local rc=$?

    # 保存日志路径供 phase 8 使用
    SCM_SELFTEST_LOG="$selftest_log"
    if [ "$rc" -ne 0 ]; then
        report_result "SCM 自检" "$rc" "自检执行失败(exit=${rc})"
    else
        report_result "SCM 自检" "$rc"
    fi
    return "$rc"
}

# 阶段 8：解析自检结果
# 从 qf_scmc check 原始输出中提取每个检查项的 名称/状态/说明，输出格式如：
#   memory               PASS     memory ok
# 原始格式特征（多行缩进 JSON）：
#   \t"memory" :              <- 检查项名行：冒号后无值（排除 "details"）
#   \t\t"message" : "memory ok",
#   \t\t"status" : "PASS"     <- status 是块内最后字段，触发一行输出
phase8_parse_report() {
    echo "================================="
    echo " 阶段 8: 自检结果"
    echo "================================="

    if [ -z "$SCM_SELFTEST_LOG" ] || [ ! -f "$SCM_SELFTEST_LOG" ]; then
        echo -e " [${R}FAIL${N}] 自检输出日志不存在"
        OVERALL_PASS=1
        FAIL_ITEMS="${FAIL_ITEMS}自检日志缺失; "
        return 1
    fi

    # 提取统计行（格式: "[OK] Self-test OK: 3 passed, 0 failed, ..."）
    local stats_line
    stats_line=$(grep 'Self-test' "$SCM_SELFTEST_LOG" | head -1)
    [ -n "$stats_line" ] && echo " $stats_line"
    echo

    # 检查项明细表格
    printf "  %-20s %-8s %s\n" "检查项" "状态" "说明"
    printf "  %s\n" "------------------------------------------------------------"
    awk '
        # 检查项名行：允许前导空白，冒号后无值（排除嵌套的 details 键）
        /^[[:space:]]*"[a-z_]+"[[:space:]]*:[[:space:]]*$/ {
            line = $0
            gsub(/^[[:space:]]*"/, "", line)
            gsub(/"[[:space:]]*:.*$/, "", line)
            if (line != "details") name = line
        }
        /"message"[[:space:]]*:/ {
            msg = $0
            sub(/.*"message"[[:space:]]*:[[:space:]]*"/, "", msg)
            sub(/".*$/, "", msg)
        }
        # status 是每个检查块的最后字段，在此触发输出并重置
        /"status"[[:space:]]*:/ {
            st = $0
            sub(/.*"status"[[:space:]]*:[[:space:]]*"/, "", st)
            sub(/".*$/, "", st)
            if (name != "") {
                printf "  %-20s %-8s %s\n", name, st, msg
                name = ""; st = ""; msg = ""
            }
        }
    ' "$SCM_SELFTEST_LOG"
    echo

    # 判断整体结果：grep "overall" 行，非 OK 标记失败
    local overall
    overall=$(grep -E '^overall:' "$SCM_SELFTEST_LOG" | awk '{print $2}')
    if [ "$overall" != "OK" ] && [ -n "$overall" ]; then
        echo -e " Overall: ${R}${overall:-UNKNOWN}${N}"
        report_result "模型推理测试" 1 "overall=${overall}"
    else
        echo -e " Overall: ${G}${overall:-UNKNOWN}${N}"
        report_result "模型推理测试" 0
    fi
}

# ===================== 主流程 =====================
parse_args "$@"

echo "##################################"
echo "# BM1684x 设备一键自检流程开始     #"
echo "# 厂商: ${VENDOR}"
echo "# 测试目录: ${TESTCHECK_DIR}"
echo "# 时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "##################################"
echo ""

# 阶段 1：硬件自检
phase1_hardware_check
echo ""

# 阶段 2：SSD 压力测试
phase2_ssd_pressure
echo ""

# 阶段 3：deb 完整性校验（失败则终止）
phase3_verify_deb
if [ "$OVERALL_PASS" -ne 0 ] && echo "$FAIL_ITEMS" | grep -q "sha256_mismatch\|deb_not_found\|sha256_not_found"; then
    echo ""
    echo -e " [${R}终止${N}] deb 包校验失败，无法继续安装"
    echo "##################################"
    echo "# 自检流程终止（deb 包不可用）      #"
    echo "##################################"
    exit 1
fi
echo ""

# 阶段 4：依赖检查（不终止）
phase4_check_deps
echo ""

# 阶段 5：安装 SCM（失败则终止）
phase5_install_scm
if echo "$FAIL_ITEMS" | grep -q "SCM安装"; then
    echo ""
    echo -e " [${R}终止${N}] SCM 安装失败，无法继续自检"
    echo "##################################"
    echo "# 自检流程终止（SCM 安装失败）      #"
    echo "##################################"
    exit 1
fi
echo ""

# 阶段 6：等待服务就绪（失败则终止）
phase6_wait_service
if echo "$FAIL_ITEMS" | grep -q "服务启动"; then
    echo ""
    echo -e " [${R}终止${N}] qf_scmd 服务未就绪，无法执行自检"
    echo "##################################"
    echo "# 自检流程终止（服务未就绪）        #"
    echo "##################################"
    exit 1
fi
echo ""

# 阶段 7：SCM 自检
phase7_scm_selftest
echo ""

# 阶段 8：解析报告
phase8_parse_report
echo ""

# ===================== 最终汇总 =====================
echo "##################################"
echo "# 共检查 ${TOTAL_COUNT} 项，通过 ${PASS_COUNT} 项，失败 ${FAIL_COUNT} 项"
echo "##################################"
for key in "${!ITEM_STATUS[@]}"; do
    _status="${ITEM_STATUS[$key]}"
    if [ "$_status" = "FAIL" ]; then
        _label=$(get_item_label "$key")
        _reason="${ITEM_REASON[$key]}"
        echo "${_label} :"
        echo -e "        结果：${R}FAIL${N} - 原因：${_reason}"
    fi
done
echo "##################################"
if [ "$OVERALL_PASS" -eq 0 ]; then
    echo -e "# ${G}整体结果: PASS${N} - 全部检测项通过  #"
else
    echo -e "# ${R}整体结果: FAIL${N} - 存在失败项      #"
fi
echo "# 完成时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "##################################"

exit "$OVERALL_PASS"
