#!/bin/bash
#
# uninstall_nginx.sh —— nginx 完全卸载脚本
# 职责：停止并卸载 nginx，清理所有相关配置与残留，尽可能恢复到安装前状态
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
# 2. 确认操作（防止误执行）
# ===========================
echo ""
echo -e "${COLOR_WARN}警告: 本脚本将完全卸载 nginx 并清理相关配置与数据！${COLOR_RESET}"
echo ""
read -r -p "是否继续？[y/N]: " confirm
if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
    log_info "操作已取消"
    exit 0
fi

# ===========================
# 3. 停止 nginx 服务
# ===========================
log_info "停止 nginx 服务 ..."
if command -v systemctl &>/dev/null; then
    systemctl stop nginx 2>/dev/null || true
    systemctl disable nginx 2>/dev/null || true
elif command -v service &>/dev/null; then
    service nginx stop 2>/dev/null || true
else
    pkill -x nginx 2>/dev/null || true
fi

if pgrep -x nginx &>/dev/null; then
    log_warn "nginx 进程仍在运行，强制终止 ..."
    pkill -9 -x nginx 2>/dev/null || true
fi
log_info "nginx 已停止"

# ===========================
# 4. 保存原始配置备份（防止 purge 删除 /etc/nginx）
# ===========================
SAVED_BACKUP_DIR=""
if ls /etc/nginx/conf.d.bak.* &>/dev/null; then
    SAVED_BACKUP_DIR="/root/nginx-conf-backup.$(date +%Y%m%d%H%M%S)"
    mkdir -p "$SAVED_BACKUP_DIR"
    cp -r /etc/nginx/conf.d.bak.* "$SAVED_BACKUP_DIR/"
    log_info "原始配置备份已暂存到: $SAVED_BACKUP_DIR"
fi

# ===========================
# 5. 卸载 nginx 软件包
# ===========================
log_info "卸载 nginx ..."

if [ -f /etc/os-release ]; then
    . /etc/os-release
    case "$ID" in
        ubuntu|debian)
            # purge 彻底移除软件包及其配置文件
            apt-get purge -y nginx nginx-common nginx-core 2>/dev/null || true
            apt-get autoremove -y 2>/dev/null || true
            ;;
        centos|rhel|rocky|almalinux|fedora)
            if command -v dnf &>/dev/null; then
                dnf remove -y nginx
            else
                yum remove -y nginx
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

if command -v nginx &>/dev/null; then
    log_warn "nginx 二进制文件仍可能存在，请手动检查"
else
    log_info "nginx 软件包已卸载"
fi

# ===========================
# 6. 清理残留配置与日志
# ===========================
log_info "清理残留文件 ..."

# 删除我们部署的配置（如果因 remove 模式导致 /etc/nginx 仍存在）
if [ -d /etc/nginx ]; then
    rm -f /etc/nginx/conf.d/test_nginx.conf
    rm -f /etc/nginx/conf.d/http_ca.conf
    rm -f /etc/nginx/conf.d/default.conf.bak.*
    rm -rf /etc/nginx/conf.d.bak.*
    log_info "已移除部署的配置文件"

    # 恢复 Debian/Ubuntu 默认站点（如果 sites-available/default 存在）
    if [ -f /etc/nginx/sites-available/default ] && [ ! -e /etc/nginx/sites-enabled/default ]; then
        ln -s /etc/nginx/sites-available/default /etc/nginx/sites-enabled/default
        log_info "已恢复默认站点链接"
    fi

    # 恢复 default.conf：从暂存的备份中找回
    if [ -n "$SAVED_BACKUP_DIR" ] && [ -d "$SAVED_BACKUP_DIR" ]; then
        # 查找备份目录下的 default.conf
        found_bak=$(find "$SAVED_BACKUP_DIR" -name "default.conf" | head -n 1 || true)
        if [ -n "$found_bak" ]; then
            cp -f "$found_bak" /etc/nginx/conf.d/default.conf
            log_info "已恢复原始 default.conf"
        fi
    fi
fi

# 清理日志与缓存（可选，彻底恢复）
if [ -d /var/log/nginx ]; then
    rm -rf /var/log/nginx
    log_info "已清理 /var/log/nginx"
fi

if [ -d /var/cache/nginx ]; then
    rm -rf /var/cache/nginx
    log_info "已清理 /var/cache/nginx"
fi

# 清理 systemd 残留状态
if command -v systemctl &>/dev/null; then
    systemctl daemon-reload 2>/dev/null || true
fi

# ===========================
# 7. 输出结果与提示
# ===========================
echo ""
echo "=========================================="
echo -e "${COLOR_INFO}nginx 已完全卸载${COLOR_RESET}"
echo "=========================================="
echo ""

if [ -n "$SAVED_BACKUP_DIR" ] && [ -d "$SAVED_BACKUP_DIR" ]; then
    echo "  原始配置备份暂存于: $SAVED_BACKUP_DIR"
    echo "  （如需保留，请手动移走；不需要可执行 rm -rf $SAVED_BACKUP_DIR）"
    echo ""
fi

echo "  注意："
echo "    1. deploy_config.sh 执行时曾修改目录权限（chmod o+x / chmod o+rX）"
echo "       这些权限变更无法自动精确回滚，如有需要请手动恢复。"
echo "    2. 前端目录 frontend/ 属于项目源码，不会被删除。"
echo "    3. 若后续需重新安装，可再次执行 install_nginx.sh。"
echo ""
echo "=========================================="

exit 0
