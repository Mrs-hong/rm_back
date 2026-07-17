#!/bin/bash
#
# install_nginx.sh —— nginx 软件安装脚本
# 职责：仅安装 nginx，不涉及任何配置部署
#

set -euo pipefail

# ===========================
# 颜色定义
# ===========================
COLOR_INFO="\033[32m"
COLOR_WARN="\033[33m"
COLOR_ERROR="\033[31m"
COLOR_RESET="\033[0m"

log_info()  { echo -e "${COLOR_INFO}[INFO] $*${COLOR_RESET}"; }
log_warn()  { echo -e "${COLOR_WARN}[WARN] $*${COLOR_RESET}"; }
log_error() { echo -e "${COLOR_ERROR}[ERROR] $*${COLOR_RESET}"; }

# ===========================
# 1. 检查 root 权限
# ===========================
if [ "$EUID" -ne 0 ]; then
    log_error "请使用 root 权限运行本脚本"
    log_info "提示: sudo bash $0"
    exit 1
fi

# ===========================
# 2. 检测系统类型并安装 nginx
# ===========================
install_nginx() {
    if command -v nginx &>/dev/null; then
        log_info "nginx 已安装: $(nginx -v 2>&1)"
        return 0
    fi

    log_info "正在安装 nginx ..."

    if [ -f /etc/os-release ]; then
        . /etc/os-release
        case "$ID" in
            ubuntu|debian)
                apt-get update -y
                apt-get install -y nginx
                ;;
            centos|rhel|rocky|almalinux|fedora)
                if command -v dnf &>/dev/null; then
                    dnf install -y nginx
                else
                    yum install -y nginx
                fi
                ;;
            *)
                log_error "不支持的操作系统: $ID"
                exit 1
                ;;
        esac
    else
        log_error "无法检测操作系统类型"
        exit 1
    fi

    if ! command -v nginx &>/dev/null; then
        log_error "nginx 安装失败"
        exit 1
    fi

    log_info "nginx 安装成功"
}

install_nginx

# ===========================
# 3. 输出结果
# ===========================
echo ""
echo "=========================================="
echo -e "${COLOR_INFO}nginx 安装完成！${COLOR_RESET}"
echo "=========================================="
echo ""
echo "  版本信息:"
nginx -v 2>&1 | sed 's/^/    /'
echo ""
echo "  下一步:"
echo "    执行 deploy_config.sh 部署配置文件并启动服务"
echo ""
echo "  常用命令:"
echo "    nginx -t                 # 测试配置"
echo "    nginx -s reload          # 重新加载配置"
echo "    systemctl status nginx   # 查看状态"
echo "    systemctl restart nginx  # 重启服务"
echo ""
echo "=========================================="

exit 0
