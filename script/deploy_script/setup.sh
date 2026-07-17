#!/bin/bash
# =============================================================================
# 一键安装部署 qf 服务 - setup.sh
# =============================================================================
# 说明：
#   以 root 权限依次执行系统库安装、数据库初始化、nginx 卸载/安装、
#   SCM/模型/nginx 配置/服务安装/华宇环境修复，共 8 个步骤，任一步骤失败则终止。
# 用法：sudo bash setup.sh
# =============================================================================

set -euo pipefail

# 禁止交互式提示（apt/dpkg 等包管理器），子进程会继承此环境变量
export DEBIAN_FRONTEND=noninteractive

# ---------------------------------------------------------------------------
# 定位脚本目录，所有子脚本路径基于此计算
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
# 总步骤数
# ---------------------------------------------------------------------------
TOTAL_STEPS=8

# ---------------------------------------------------------------------------
# 日志函数（风格与 database/common.sh 一致）
# ---------------------------------------------------------------------------
log_info() {
    echo "[INFO] $(date '+%Y-%m-%d %H:%M:%S') $*"
}

log_error() {
    echo "[ERROR] $(date '+%Y-%m-%d %H:%M:%S') $*" >&2
}

log_warn() {
    echo "[WARN] $(date '+%Y-%m-%d %H:%M:%S') $*" >&2
}

# ---------------------------------------------------------------------------
# 执行子脚本的封装函数
# 参数：$1=步骤序号, $2=描述, $3=执行命令（字符串）
# ---------------------------------------------------------------------------
run_step() {
    local step_num="$1"
    local desc="$2"
    local cmd="$3"

    log_info "[$step_num/$TOTAL_STEPS] 正在执行: $desc"

    if eval "$cmd"; then
        log_info "[$step_num/$TOTAL_STEPS] 完成: $desc"
    else
        exit_code=$?
        log_error "[$step_num/$TOTAL_STEPS] 失败: $desc (退出码: $exit_code)"
        exit "$exit_code"
    fi
}

# ---------------------------------------------------------------------------
# 检查 root 权限
# ---------------------------------------------------------------------------
if [[ $EUID -ne 0 ]]; then
    log_error "需要 root 权限运行此脚本，请使用 sudo"
    exit 1
fi

log_info "========== qf 服务一键部署开始 =========="

# 步骤1/8：安装系统库
run_step 1 "安装系统库" "bash '${SCRIPT_DIR}/sys_lib/install.sh'"

# 步骤2/8：安装并初始化数据库
run_step 2 "安装并初始化数据库" "bash '${SCRIPT_DIR}/database/install.sh'"

# 步骤3/8：卸载旧 nginx（uninstall_nginx.sh 含交互式确认，通过管道传入 "y"）
run_step 3 "卸载旧 nginx" "echo 'y' | bash '${SCRIPT_DIR}/nginx/uninstall_nginx.sh'"

# 步骤4/8：安装 nginx
run_step 4 "安装 nginx" "bash '${SCRIPT_DIR}/nginx/install_nginx.sh'"

# 步骤5/8：安装 SCM 及模型（install.sh 内部继续完成 init_nginx 和安装服务）
run_step 5 "scm安装模型 model_qifeng_ca 等" "bash '${SCRIPT_DIR}/scm_and_soft/install.sh'"

# 步骤8/8：安装华宇环境修复服务（MySQL权限修复、磁盘挂载、业务重启，systemd 启动时生效）
run_step 8 "安装华宇环境修复服务 fix_hua_yu" "bash '${SCRIPT_DIR}/fix_hua_yu.sh'"

log_info "========== qf 服务一键部署完成（全部 $TOTAL_STEPS 步骤成功）=========="
