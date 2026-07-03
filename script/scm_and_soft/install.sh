#!/bin/bash
# =============================================================================
# SCM 及配套软件服务安装脚本 - scm_and_soft/install.sh
# =============================================================================
# 说明：
#   安装 qifeng_scm deb 包，再按 install_order.txt 逐项安装并可选启动服务。
# =============================================================================

set -euo pipefail

# 禁止交互式提示（apt/dpkg 等包管理器）
export DEBIAN_FRONTEND=noninteractive

# -----------------------------------------------------------------------------
# 日志函数（与 database/common.sh 风格一致）
# -----------------------------------------------------------------------------
log_info()  { echo "[INFO] $(date '+%Y-%m-%d %H:%M:%S') $*"; }
log_error() { echo "[ERROR] $(date '+%Y-%m-%d %H:%M:%S') $*" >&2; }
log_warn()  { echo "[WARN] $(date '+%Y-%m-%d %H:%M:%S') $*" >&2; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# -----------------------------------------------------------------------------
# 检查 root 权限
# -----------------------------------------------------------------------------
if [[ $EUID -ne 0 ]]; then
    log_error "请使用 root 权限运行本脚本"
    exit 1
fi

# ---------------------------------------------------------------------------
# [1/4] 查找并安装 SCM deb 包
# ---------------------------------------------------------------------------
log_info "===== [1/4] 安装 SCM ====="

deb_files=("${SCRIPT_DIR}"/qifeng_scm*.deb)

if [[ ${#deb_files[@]} -eq 0 ]] || [[ ! -f "${deb_files[0]}" ]]; then
    log_error "未找到 qifeng_scm-*.deb 安装包，请确认文件位于: ${SCRIPT_DIR}"
    exit 1
fi

if [[ ${#deb_files[@]} -gt 1 ]]; then
    log_warn "发现多个 deb 包，将使用: ${deb_files[0]}"
fi

DEB_PATH="${deb_files[0]}"
log_info "找到安装包: ${DEB_PATH}"

log_info "正在卸载旧版 SCM (保留数据)..."
dpkg -r qifeng_scm 2>/dev/null || true

log_info "正在安装 SCM (dpkg)..."
if ! dpkg -i "${DEB_PATH}"; then
    log_error "dpkg 安装 SCM 失败"
    exit 1
fi

log_info "正在修复依赖..."
if ! apt-get install -f -y; then
    log_error "apt-get install -f 修复依赖失败"
    exit 1
fi

# ---------------------------------------------------------------------------
# [2/4] 验证 qf_scmc 命令可用
# ---------------------------------------------------------------------------
log_info "===== [2/4] 验证 SCM 命令 ====="

# 等待 SCM 启动初始化完成（轮询检测 scmd socket 是否就绪，最多等待 30 秒）
SCMD_SOCK="/run/qifeng-scm/scmd.sock"
SOCK_READY=false
for i in $(seq 1 30); do
    if [[ -S "${SCMD_SOCK}" ]]; then
        SOCK_READY=true
        log_info "scmd socket 已就绪 (等待 ${i} 秒)"
        break
    fi
    sleep 1
done

if [[ "${SOCK_READY}" != "true" ]]; then
    log_error "scmd socket 未就绪，等待超时 (${SCMD_SOCK})"
    exit 1
fi

if ! command -v qf_scmc &>/dev/null; then
    log_error "qf_scmc 命令不可用，SCM 安装可能失败"
    exit 1
fi
log_info "SCM 安装成功，qf_scmc 已可用"

# ---------------------------------------------------------------------------
# [3/4] 按 install_order.txt 逐项安装服务
# ---------------------------------------------------------------------------
log_info "===== [3/4] 安装服务 ====="

ORDER_FILE="${SCRIPT_DIR}/install_order.txt"
if [[ ! -f "${ORDER_FILE}" ]]; then
    log_error "未找到安装顺序配置: ${ORDER_FILE}"
    exit 1
fi

# 统计有效服务行数，用于步骤编号
total_services=0
while IFS= read -r line || [[ -n "$line" ]]; do
    [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
    read -r s1 s2 _ <<< "$line"
    [[ -n "$s1" && -n "$s2" ]] && total_services=$((total_services + 1))
done < "$ORDER_FILE"

svc_idx=0
while IFS= read -r line || [[ -n "$line" ]]; do
    [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue

    read -r svc_name pkg_name auto_start <<< "$line"

    if [[ -z "$svc_name" || -z "$pkg_name" ]]; then
        log_warn "跳过无效行: $line"
        continue
    fi

    svc_idx=$((svc_idx + 1))
    pkg_path="${SCRIPT_DIR}/${pkg_name}"

    if [[ ! -f "${pkg_path}" ]]; then
        log_error "软件包不存在: ${pkg_path}，停止安装"
        exit 1
    fi

    # 先卸载已存在的同名服务（忽略卸载失败，可能服务未安装）
    log_info "[${svc_idx}/${total_services}] 尝试卸载已存在的服务: ${svc_name} ..."
    qf_scmc uninstall -n "${svc_name}" 2>/dev/null || true

    log_info "[${svc_idx}/${total_services}] 正在安装服务: ${svc_name} (包: ${pkg_name}) ..."
    if ! qf_scmc install -n "${svc_name}" --tar_dir "${pkg_path}"; then
        log_error "安装服务 ${svc_name} 失败，停止安装"
        exit 1
    fi
    log_info "[${svc_idx}/${total_services}] 服务 ${svc_name} 安装成功"

    # 根据配置决定是否启动
    if [[ "${auto_start}" == "true" ]]; then
        log_info "[${svc_idx}/${total_services}] 正在启动服务: ${svc_name} ..."
        sleep 2
        if ! qf_scmc start -n "${svc_name}"; then
            log_error "启动服务 ${svc_name} 失败，停止安装"
            exit 1
        fi
        log_info "[${svc_idx}/${total_services}] 服务 ${svc_name} 已启动"
    else
        log_info "[${svc_idx}/${total_services}] 服务 ${svc_name} 配置为不自动启动，跳过启动"
    fi

done < "$ORDER_FILE"

# ---------------------------------------------------------------------------
# [4/4] 验证所有服务状态
# ---------------------------------------------------------------------------
log_info "===== [4/4] 验证服务状态 ====="

qf_scmc list
log_info "所有服务安装完成"

exit 0
