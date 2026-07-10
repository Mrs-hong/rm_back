#!/bin/bash
# =============================================================================
# MariaDB 一键部署脚本 - database/install.sh
# =============================================================================
# 说明：
#   读取 mariadb/config.txt 配置，依次执行卸载、安装、初始化，
#   实现MariaDB数据库的完整部署流程。
#
# 用法：
#   install.sh [选项]
#
# 选项：
#   --help, -h    显示此帮助信息
#
# 返回值：
#   0 成功
#   1 通用错误
#   2 参数错误（config.txt 缺少必填字段）
#   3 权限不足
# =============================================================================

set -euo pipefail

# 禁止交互式提示（apt/dpkg 等包管理器）
export DEBIAN_FRONTEND=noninteractive

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

# 解析参数
db_parse_args "$@"

# 帮助信息
if [[ -n "$(db_arg "help" "")" || -n "$(db_arg "h" "")" ]]; then
    db_print_help "install.sh" "MariaDB 一键部署脚本（卸载+安装+初始化）" \
"  --help, -h    显示此帮助信息"
    exit $DB_EXIT_SUCCESS
fi

# 检查 root 权限
db_check_root

# ---------------------------------------------------------------------------
# 读取 config.txt 配置
# 格式：key = value（等号两边可能有空格），忽略空行和注释行
# ---------------------------------------------------------------------------
CONFIG_FILE="${SCRIPT_DIR}/mariadb/config.txt"
if [[ ! -f "$CONFIG_FILE" ]]; then
    db_exit_with_error $DB_EXIT_INVALID_ARGS "配置文件不存在: $CONFIG_FILE"
fi

db_log_info "读取配置文件: $CONFIG_FILE"

declare -A config_map
while IFS= read -r line || [[ -n "$line" ]]; do
    [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
    if [[ "$line" =~ ^[[:space:]]*([^=]+)[[:space:]]*=[[:space:]]*(.*)[[:space:]]*$ ]]; then
        key="${BASH_REMATCH[1]}"
        val="${BASH_REMATCH[2]}"
        key="${key#"${key%%[![:space:]]*}"}"
        key="${key%"${key##*[![:space:]]}"}"
        val="${val#"${val%%[![:space:]]*}"}"
        val="${val%"${val##*[![:space:]]}"}"
        config_map["$key"]="$val"
    fi
done < "$CONFIG_FILE"

# admin_password 为必填字段
ADMIN_PASSWORD="${config_map[admin_password]:-}"
if [[ -z "$ADMIN_PASSWORD" ]]; then
    db_exit_with_error $DB_EXIT_INVALID_ARGS "config.txt 缺少必填字段: admin_password"
fi

db_log_info "配置项加载完成，admin_password 已读取"

# ---------------------------------------------------------------------------
# 步骤1/3：执行卸载（--purge 彻底清理旧环境）
# ---------------------------------------------------------------------------
db_log_info "===== [1/3] 卸载旧 MariaDB 环境 ====="
bash "${SCRIPT_DIR}/mariadb/uninstall.sh" --purge
db_log_info "[1/3] 卸载完成"

# ---------------------------------------------------------------------------
# 步骤2/3：安装 MariaDB
# ---------------------------------------------------------------------------
db_log_info "===== [2/3] 安装 MariaDB ====="
bash "${SCRIPT_DIR}/mariadb/install.sh"
db_log_info "[2/3] 安装完成"

# ---------------------------------------------------------------------------
# 步骤3/3：初始化 MariaDB，根据 config.txt 动态构建参数
# ---------------------------------------------------------------------------
db_log_info "===== [3/3] 初始化 MariaDB ====="

INIT_ARGS=("--admin_password=${ADMIN_PASSWORD}")

if [[ -n "${config_map[data]:-}" ]]; then
    INIT_ARGS+=("--data=${config_map[data]}")
fi

if [[ "${config_map[force]:-}" == "true" ]]; then
    INIT_ARGS+=("--force")
fi

if [[ -n "${config_map[port]:-}" ]]; then
    INIT_ARGS+=("--port=${config_map[port]}")
fi

if [[ -n "${config_map[ip]:-}" ]]; then
    INIT_ARGS+=("--ip=${config_map[ip]}")
fi

if [[ -n "${config_map[character_set]:-}" ]]; then
    INIT_ARGS+=("--character_set=${config_map[character_set]}")
fi

db_log_info "初始化参数: ${INIT_ARGS[*]}"
bash "${SCRIPT_DIR}/mariadb/init.sh" "${INIT_ARGS[@]}"
db_log_info "[3/3] 初始化完成"

db_log_info "===== MariaDB 一键部署完成 ====="
exit $DB_EXIT_SUCCESS
