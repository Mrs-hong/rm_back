#!/bin/bash
# =============================================================================
# MariaDB 卸载脚本
# =============================================================================
# 说明：
#   卸载 MariaDB 数据库服务，可选是否同时删除数据目录和配置文件。
#
# 用法：
#   uninstall.sh [选项]
#
# 选项：
#   --purge       同时删除数据目录和配置文件（可选）
#   --help, -h    显示此帮助信息
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

# 帮助信息
if [[ -n "$(db_arg "help" "")" || -n "$(db_arg "h" "")" ]]; then
    db_print_help "uninstall.sh" "卸载 MariaDB 数据库服务" \
"  --purge       同时删除数据目录和配置文件（可选）
  --help, -h    显示此帮助信息"
    exit $DB_EXIT_SUCCESS
fi

# 检查 root 权限
db_check_root

# 先停止服务
db_log_info "停止 MariaDB 服务..."
if db_command_exists systemctl; then
    systemctl stop mariadb 2>/dev/null || systemctl stop mysql 2>/dev/null || true
    systemctl disable mariadb 2>/dev/null || systemctl disable mysql 2>/dev/null || true
else
    service mysql stop 2>/dev/null || service mariadb stop 2>/dev/null || true
fi

# 卸载软件包
# 仅在检测到已安装时才执行卸载，避免重复执行出错；卸载失败时真实报错
db_log_info "卸载 MariaDB 软件包..."
if dpkg -l 2>/dev/null | grep -qiE 'mariadb-server|mysql-server'; then
    if [[ -n "$PURGE" ]]; then
        apt-get remove --purge -y mariadb-server mariadb-client mysql-server mysql-client || {
            db_exit_with_error $DB_EXIT_GENERIC_ERROR "卸载 MariaDB 软件包失败"
        }
        apt-get autoremove -y 2>/dev/null || true
    else
        apt-get remove -y mariadb-server mariadb-client mysql-server mysql-client || {
            db_exit_with_error $DB_EXIT_GENERIC_ERROR "卸载 MariaDB 软件包失败"
        }
    fi
else
    db_log_info "未检测到 MariaDB 软件包，跳过卸载步骤"
fi

# 可选：清理数据目录和日志（这些不由 dpkg 管理）
if [[ -n "$PURGE" ]]; then
    db_log_info "清理数据目录和日志..."

    # 读取自定义数据目录（init.sh 可能配置了非默认路径）
    CUSTOM_DATA_DIR=""
    if [[ -f /etc/mysql/mariadb.conf.d/99-custom.cnf ]]; then
        CUSTOM_DATA_DIR=$(grep -E '^datadir\s*=' /etc/mysql/mariadb.conf.d/99-custom.cnf 2>/dev/null | head -1 | sed 's/^datadir\s*=\s*//')
    fi

    # 清理默认数据目录
    if [[ -d /var/lib/mysql ]]; then
        db_safe_rm "/var/lib/mysql" "默认数据目录" || db_log_warn "无法删除 /var/lib/mysql"
    fi

    # 清理自定义数据目录（如果与默认目录不同）
    if [[ -n "$CUSTOM_DATA_DIR" && "$CUSTOM_DATA_DIR" != "/var/lib/mysql" && -d "$CUSTOM_DATA_DIR" ]]; then
        db_safe_rm "$CUSTOM_DATA_DIR" "自定义数据目录" || db_log_warn "无法删除自定义数据目录 $CUSTOM_DATA_DIR"
    fi

    if [[ -d /var/log/mysql ]]; then
        db_safe_rm "/var/log/mysql" "日志目录" || db_log_warn "无法删除 /var/log/mysql"
    fi

    # 删除 init.sh 创建的 drop-in 配置文件（不由 dpkg 管理，apt remove --purge 不会删除）
    if [[ -f /etc/mysql/mariadb.conf.d/99-custom.cnf ]]; then
        rm -f /etc/mysql/mariadb.conf.d/99-custom.cnf || db_log_warn "无法删除 99-custom.cnf"
    fi
    if [[ -d /etc/systemd/system/mariadb.service.d ]]; then
        rm -rf /etc/systemd/system/mariadb.service.d || db_log_warn "无法删除 mariadb.service.d"
    fi
    systemctl daemon-reload 2>/dev/null || true

    # 仅当 /etc/mysql 不由 dpkg 管理时才清理整个目录（兼容非 apt 安装场景）
    if [[ -d /etc/mysql ]]; then
        if dpkg -S /etc/mysql >/dev/null 2>&1; then
            db_log_info "/etc/mysql 由 dpkg 管理，跳过删除"
        else
            db_safe_rm "/etc/mysql" "配置目录" || db_log_warn "无法删除 /etc/mysql"
            rm -f /etc/my.cnf 2>/dev/null || true
            db_safe_rm "/etc/my.cnf.d" "配置目录" 2>/dev/null || true
        fi
    fi
fi

db_log_info "MariaDB 卸载完成"
exit $DB_EXIT_SUCCESS
