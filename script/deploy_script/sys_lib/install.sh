#!/bin/bash
# =============================================================================
# 系统依赖库安装脚本 - sys_lib/install.sh
# =============================================================================
# 说明：
#   安装 qifeng-scm 所需的系统级依赖库（openssl、zlib、systemd 等）。
#   支持 Ubuntu/Debian 和 CentOS/RHEL 系列，自动检测发行版并选择对应包管理器。
#   幂等设计：已安装的包会被跳过，重复执行不会报错。
# =============================================================================

set -euo pipefail

# 禁止交互式提示（apt/dpkg 等包管理器）
export DEBIAN_FRONTEND=noninteractive

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# -----------------------------------------------------------------------------
# 日志函数（与 database/common.sh 风格一致）
# -----------------------------------------------------------------------------
log_info()  { echo "[INFO] $(date '+%Y-%m-%d %H:%M:%S') $*"; }
log_error() { echo "[ERROR] $(date '+%Y-%m-%d %H:%M:%S') $*" >&2; }
log_warn()  { echo "[WARN] $(date '+%Y-%m-%d %H:%M:%S') $*" >&2; }

exit_with_error() {
    local code=$1; shift
    log_error "$*"
    exit "$code"
}

readonly EXIT_SUCCESS=0
readonly EXIT_NO_ROOT=1
readonly EXIT_UNSUPPORTED_OS=2
readonly EXIT_INSTALL_FAILED=3

# -----------------------------------------------------------------------------
# 检查 root 权限
# -----------------------------------------------------------------------------
check_root() {
    if [[ $EUID -ne 0 ]]; then
        exit_with_error $EXIT_NO_ROOT "需要 root 权限运行此脚本，请使用 sudo"
    fi
    log_info "权限检查通过"
}

# -----------------------------------------------------------------------------
# 检测操作系统类型（基于 /etc/os-release）
# 返回：ubuntu | debian | centos | rhel | rocky | almalinux | unknown
# -----------------------------------------------------------------------------
detect_os() {
    if [[ ! -f /etc/os-release ]]; then
        exit_with_error $EXIT_UNSUPPORTED_OS "无法检测操作系统：/etc/os-release 不存在"
    fi
    # shellcheck source=/dev/null
    source /etc/os-release
    local os_id
    os_id=$(echo "$ID" | tr '[:upper:]' '[:lower:]')
    echo "$os_id"
}

# -----------------------------------------------------------------------------
# Debian/Ubuntu 系列安装
# -----------------------------------------------------------------------------
install_debian_family() {
    log_info "检测到 Debian 系列发行版，开始安装依赖..."

    log_info "[1/3] 更新包索引 (apt update)..."
    apt-get update -qq || exit_with_error $EXIT_INSTALL_FAILED "apt-get update 失败"

    local packages=(
        protobuf-compiler
        uuid-dev
        binutils 
        libpq-dev 
        libcurl4-openssl-dev 
        default-libmysqlclient-dev 
        libgpiod-dev 
        libasound2-dev 
        libsystemd-dev
        zlib1g-dev
    )

    local total=${#packages[@]}
    log_info "[2/3] 准备安装 ${total} 个依赖包"

    # 统计已安装数量，用于步骤编号
    local already_installed=0
    local to_install=()
    for pkg in "${packages[@]}"; do
        if dpkg -s "$pkg" &>/dev/null; then
            already_installed=$((already_installed + 1))
        else
            to_install+=("$pkg")
        fi
    done

    if [[ $already_installed -gt 0 ]]; then
        log_info "已有 ${already_installed}/${total} 个包安装，跳过"
    fi

    # 逐个安装未安装的包
    local failed=()
    local idx=0
    for pkg in "${to_install[@]}"; do
        idx=$((idx + 1))
        log_info "[2/3] 正在安装 [${idx}/${#to_install[@]}]: $pkg"
        if ! apt-get install -y -qq "$pkg"; then
            failed+=("$pkg")
            log_error "安装失败: $pkg"
        fi
    done

    if [[ ${#failed[@]} -gt 0 ]]; then
        exit_with_error $EXIT_INSTALL_FAILED "以下包安装失败: ${failed[*]}"
    fi

    log_info "[3/3] Debian 系列依赖安装完成（共 ${total} 个包）"
}

# -----------------------------------------------------------------------------
# CentOS/RHEL 系列安装
# -----------------------------------------------------------------------------
install_rhel_family() {
    log_info "检测到 RHEL 系列发行版，开始安装依赖..."

    local pkg_mgr
    if command -v dnf &>/dev/null; then
        pkg_mgr="dnf"
    elif command -v yum &>/dev/null; then
        pkg_mgr="yum"
    else
        exit_with_error $EXIT_INSTALL_FAILED "未找到 dnf 或 yum 包管理器"
    fi

    log_info "[1/4] 更新包索引 ($pkg_mgr makecache)..."
    $pkg_mgr makecache -y --quiet || log_warn "包索引更新失败，继续安装"

    local packages=(
        protobuf-compiler
        uuid-dev
        binutils 
        libpq-dev 
        libcurl4-openssl-dev 
        default-libmysqlclient-dev 
        libgpiod-dev 
        libasound2-dev 
        libsystemd-dev
        zlib1g-dev
    )

    # EPEL 仓库（gtest-devel 等包需要）
    log_info "[2/4] 检查 EPEL 仓库..."
    if ! rpm -q epel-release &>/dev/null; then
        log_info "安装 EPEL 仓库..."
        $pkg_mgr install -y -q epel-release 2>/dev/null || log_warn "EPEL 安装失败，部分包可能不可用"
    else
        log_info "EPEL 仓库已安装"
    fi

    local total=${#packages[@]}
    log_info "[3/4] 准备安装 ${total} 个依赖包"

    local already_installed=0
    local to_install=()
    for pkg in "${packages[@]}"; do
        if rpm -q "$pkg" &>/dev/null; then
            already_installed=$((already_installed + 1))
        else
            to_install+=("$pkg")
        fi
    done

    if [[ $already_installed -gt 0 ]]; then
        log_info "已有 ${already_installed}/${total} 个包安装，跳过"
    fi

    local failed=()
    local idx=0
    for pkg in "${to_install[@]}"; do
        idx=$((idx + 1))
        log_info "[3/4] 正在安装 [${idx}/${#to_install[@]}]: $pkg"
        if ! $pkg_mgr install -y -q "$pkg"; then
            failed+=("$pkg")
            log_error "安装失败: $pkg"
        fi
    done

    if [[ ${#failed[@]} -gt 0 ]]; then
        exit_with_error $EXIT_INSTALL_FAILED "以下包安装失败: ${failed[*]}"
    fi

    log_info "[4/4] RHEL 系列依赖安装完成（共 ${total} 个包）"
}

# =============================================================================
# 主流程
# =============================================================================
main() {
    log_info "===== qifeng-scm 系统依赖安装开始 ====="

    check_root

    local os_id
    os_id=$(detect_os)
    log_info "检测到操作系统: $os_id"

    case "$os_id" in
        ubuntu|debian|linuxmint|pop)
            install_debian_family
            ;;
        centos|rhel|rocky|almalinux|fedora|ol)
            install_rhel_family
            ;;
        *)
            exit_with_error $EXIT_UNSUPPORTED_OS "不支持的操作系统: $os_id"
            ;;
    esac

    log_info "===== qifeng-scm 系统依赖安装完成 ====="
}

main "$@"
