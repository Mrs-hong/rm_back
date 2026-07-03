#!/bin/bash
#
# deploy_config.sh —— nginx 配置部署脚本
# 职责：拷贝配置文件、修复权限、测试并重载 nginx
# 前提：nginx 已安装（先执行 install_nginx.sh）
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
# 1. 计算脚本所在目录（即 test_nginx 根目录）
# ===========================
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
log_info "test_nginx root: $SCRIPT_DIR"

# ===========================
# 2. 检查 root 权限
# ===========================
if [ "$EUID" -ne 0 ]; then
    log_error "请使用 root 权限运行本脚本（nginx 绑定 80 端口需要 root）"
    log_info "提示: sudo bash $0"
    exit 1
fi

# ===========================
# 3. 检查 nginx 是否已安装
# ===========================
if ! command -v nginx &>/dev/null; then
    log_error "nginx 未安装，请先执行 install_nginx.sh"
    exit 1
fi
log_info "nginx 已安装: $(nginx -v 2>&1)"

# ===========================
# 4. 检查前端资源是否存在
# ===========================
if [ ! -f "$SCRIPT_DIR/frontend/index.html" ]; then
    log_error "未找到前端入口文件: $SCRIPT_DIR/frontend/index.html"
    log_info "请确保 frontend/ 目录包含完整的构建产物（index.html + assets/）"
    exit 1
fi
log_info "前端资源检查通过"

# ===========================
# 5. 备份原有配置
# ===========================
NGINX_CONF_DIR="/etc/nginx/conf.d"
BACKUP_DIR="/etc/nginx/conf.d.bak.$(date +%Y%m%d%H%M%S)"

if [ -f "$NGINX_CONF_DIR/default.conf" ]; then
    log_info "备份原有配置到 $BACKUP_DIR"
    mkdir -p "$BACKUP_DIR"
    cp -p "$NGINX_CONF_DIR/default.conf" "$BACKUP_DIR/"
fi

# ===========================
# 6. 部署 nginx 配置
# ===========================
log_info "部署 nginx 配置文件 ..."

# 将配置模板中的占位符替换为实际路径
mkdir -p "$NGINX_CONF_DIR"
sed "s|__TEST_NGINX_ROOT__|$SCRIPT_DIR|g" \
    "$SCRIPT_DIR/conf.d/default.conf" \
    > "$NGINX_CONF_DIR/test_nginx.conf"

# 部署后端 API 代理配置（独立文件，便于维护）
# 放到 snippets 目录，避免被 nginx.conf 的 "include conf.d/*.conf" 自动加载到 http 块
NGINX_SNIPPETS_DIR="/etc/nginx/snippets"
if [ -f "$SCRIPT_DIR/conf.d/http_ca.conf" ]; then
    mkdir -p "$NGINX_SNIPPETS_DIR"
    cp -f "$SCRIPT_DIR/conf.d/http_ca.conf" "$NGINX_SNIPPETS_DIR/http_ca.conf"
    log_info "后端代理配置已部署到: $NGINX_SNIPPETS_DIR/http_ca.conf"
fi

# 清理旧版本残留：若 http_ca.conf 仍在 conf.d 中，会被自动加载到 http 块导致报错
if [ -f "$NGINX_CONF_DIR/http_ca.conf" ]; then
    log_warn "发现旧版本残留 $NGINX_CONF_DIR/http_ca.conf，正在移除 ..."
    rm -f "$NGINX_CONF_DIR/http_ca.conf"
fi

# 若存在旧的 default.conf，为避免 80 端口冲突，先移除或重命名
if [ -f "$NGINX_CONF_DIR/default.conf" ]; then
    log_warn "发现已有的 default.conf，为避免 80 端口冲突，将其重命名"
    mv "$NGINX_CONF_DIR/default.conf" "$NGINX_CONF_DIR/default.conf.bak.$(date +%s)"
fi

log_info "主配置文件已部署到: $NGINX_CONF_DIR/test_nginx.conf"

# ===========================
# 7. 修复目录权限（确保 nginx worker 能访问前端文件）
# ===========================
fix_permissions() {
    log_info "修复目录权限 ..."

    # nginx worker 用户（通常是 www-data 或 nginx）
    local NGINX_USER
    NGINX_USER=$(grep -E "^user\s+" /etc/nginx/nginx.conf | awk '{print $2}' | tr -d ';' || echo "www-data")
    log_info "nginx worker 用户: $NGINX_USER"

    # 从 / 开始遍历到 frontend 目录，确保路径上每个目录对其他人有 x 权限
    local TARGET_DIR="$SCRIPT_DIR/frontend"
    local CHECK_DIR="$TARGET_DIR"

    while [ "$CHECK_DIR" != "/" ]; do
        local PERM
        PERM=$(stat -c "%a" "$CHECK_DIR" 2>/dev/null || echo "000")
        # 检查其他用户是否有 x 权限（最后一位 >= 1）
        local OTHER_X=$((PERM % 10 % 2))
        if [ "$OTHER_X" -eq 0 ]; then
            log_warn "目录 $CHECK_DIR 缺少其他用户执行权限，正在添加 ..."
            chmod o+x "$CHECK_DIR"
        fi
        CHECK_DIR=$(dirname "$CHECK_DIR")
    done

    # 确保 frontend 目录及文件对其他人可读
    chmod -R o+rX "$TARGET_DIR"
    log_info "权限修复完成"
}

fix_permissions

# ===========================
# 8. 移除 Debian/Ubuntu 默认站点（避免 80 端口冲突）
# ===========================
if [ -e /etc/nginx/sites-enabled/default ]; then
    log_warn "移除默认站点 /etc/nginx/sites-enabled/default，避免 80 端口冲突"
    rm -f /etc/nginx/sites-enabled/default
fi

# ===========================
# 9. 测试配置
# ===========================
log_info "测试 nginx 配置 ..."
if ! nginx -t; then
    log_error "nginx 配置测试失败，请检查配置"
    exit 1
fi
log_info "nginx 配置测试通过"

# ===========================
# 10. 启动/重启 nginx
# ===========================
log_info "启动 nginx 服务 ..."

if command -v systemctl &>/dev/null; then
    systemctl enable nginx --now || true
    systemctl restart nginx
else
    # 无 systemd 环境，直接启动
    nginx -s reload 2>/dev/null || nginx
fi

if pgrep -x nginx &>/dev/null; then
    log_info "nginx 服务已启动"
else
    log_error "nginx 服务启动失败"
    exit 1
fi

# ===========================
# 11. 输出访问信息
# ===========================
LOCAL_IP=$(hostname -I | awk '{print $1}')

echo ""
echo "=========================================="
echo -e "${COLOR_INFO}nginx 配置部署成功！${COLOR_RESET}"
echo "=========================================="
echo ""
echo "  访问地址:"
echo "    http://$LOCAL_IP/"
echo "    http://localhost/"
echo ""
echo "  前端目录: $SCRIPT_DIR/frontend"
echo "  主配置文件: $NGINX_CONF_DIR/test_nginx.conf"
echo "  后端代理配置: /etc/nginx/snippets/http_ca.conf"
echo ""
echo "  常用命令:"
echo "    nginx -t                 # 测试配置"
echo "    nginx -s reload          # 重新加载配置"
echo "    systemctl status nginx   # 查看状态"
echo "    systemctl restart nginx  # 重启服务"
echo ""
echo "=========================================="

exit 0
