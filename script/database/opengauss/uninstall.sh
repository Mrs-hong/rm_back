#!/bin/bash
# =============================================================================
# openGauss 卸载脚本
# =============================================================================
# 说明：
#   卸载 openGauss 数据库服务，可选是否同时删除数据目录、安装目录和操作系统用户。
#
# 用法：
#   uninstall.sh [选项]
#
# 选项：
#   --purge             同时删除数据目录、安装目录和操作系统用户（可选）
#   --install_path=DIR  安装目录（可选，默认 /opt/opengauss）
#   --user=OS_USER      操作系统用户（可选，默认 omm）
#   --data=DIR          数据目录（可选，默认 /var/lib/opengauss/data）
#   --help, -h          显示此帮助信息
#
# 返回值：
#   0 成功
#   3 权限不足
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../common.sh
source "${SCRIPT_DIR}/../common.sh"

# 解析参数
db_parse_args "$@"

PURGE=$(db_arg "purge" "")
INSTALL_PATH=$(db_arg "install_path" "/opt/opengauss")
OS_USER=$(db_arg "user" "omm")
DATA_DIR=$(db_arg "data" "/var/lib/opengauss/data")

# 帮助信息
if [[ -n "$(db_arg "help" "")" || -n "$(db_arg "h" "")" ]]; then
    db_print_help "uninstall.sh" "卸载 openGauss 数据库服务" \
"  --purge             同时删除数据目录、安装目录和操作系统用户（可选）
  --install_path=DIR  安装目录（可选，默认 /opt/opengauss）
  --user=OS_USER      操作系统用户（可选，默认 omm）
  --data=DIR          数据目录（可选，默认 /var/lib/opengauss/data）
  --help, -h          显示此帮助信息"
    exit $DB_EXIT_SUCCESS
fi

# 检查 root 权限
db_check_root

# 先停止服务
db_log_info "停止 openGauss 服务..."
if [[ "$OS_USER" == "root" ]] || id "$OS_USER" >/dev/null 2>&1; then
    db_run_as_user "$OS_USER" "
        export GAUSSHOME=${INSTALL_PATH}
        export PATH=\$GAUSSHOME/bin:\$PATH
        gs_ctl stop -D '$DATA_DIR' -m fast 2>/dev/null || true
    " 2>/dev/null || true
fi

# 删除安装目录
if [[ -d "$INSTALL_PATH" ]]; then
    db_log_info "删除安装目录: $INSTALL_PATH"
    rm -rf "$INSTALL_PATH"
fi

# 清理环境变量配置
rm -f /etc/profile.d/opengauss.sh

# 可选：彻底清理
if [[ -n "$PURGE" ]]; then
    db_log_info "清理数据目录..."
    rm -rf "$DATA_DIR" /var/lib/opengauss /var/log/opengauss

    if [[ "$OS_USER" != "root" ]]; then
        db_log_info "删除操作系统用户和组: $OS_USER"
        userdel -r "$OS_USER" 2>/dev/null || true
        groupdel "$OS_USER" 2>/dev/null || true
    fi
fi

db_log_info "openGauss 卸载完成"
exit $DB_EXIT_SUCCESS
