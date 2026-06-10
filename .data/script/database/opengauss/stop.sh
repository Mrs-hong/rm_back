#!/bin/bash
# =============================================================================
# openGauss 停止脚本
# =============================================================================
# 说明：
#   停止 openGauss 数据库服务。
#
# 用法：
#   stop.sh [选项]
#
# 选项：
#   --data=DIR        数据目录（可选，默认 /var/lib/opengauss/data）
#   --user=OS_USER    运行用户（可选，默认 omm）
#   --install_path=DIR 安装目录（可选，默认 /opt/opengauss）
#   --mode=MODE       停止模式：smart|fast|immediate（可选，默认 fast）
#   --help, -h        显示此帮助信息
#
# 返回值：
#   0 成功
#   3 权限不足
#   5 服务未运行
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../common.sh
source "${SCRIPT_DIR}/../common.sh"

# 解析参数
db_parse_args "$@"

DATA_DIR=$(db_arg "data" "/var/lib/opengauss/data")
OS_USER=$(db_arg "user" "omm")
INSTALL_PATH=$(db_arg "install_path" "/opt/opengauss")
MODE=$(db_arg "mode" "fast")

# 帮助信息
if [[ -n "$(db_arg "help" "")" || -n "$(db_arg "h" "")" ]]; then
    db_print_help "stop.sh" "停止 openGauss 数据库服务" \
"  --data=DIR          数据目录（可选，默认 /var/lib/opengauss/data）
  --user=OS_USER      运行用户（可选，默认 omm）
  --install_path=DIR  安装目录（可选，默认 /opt/opengauss）
  --mode=MODE         停止模式：smart|fast|immediate（可选，默认 fast）
  --help, -h          显示此帮助信息"
    exit $DB_EXIT_SUCCESS
fi

# 权限检查
if [[ $EUID -ne 0 ]]; then
    if [[ "$(whoami)" != "$OS_USER" ]]; then
        db_exit_with_error $DB_EXIT_PERMISSION_DENIED "需要 root 权限或 ${OS_USER} 用户运行"
    fi
fi

# 检查数据目录
if [[ ! -d "$DATA_DIR" ]]; then
    db_exit_with_error $DB_EXIT_MISSING_DEPEND "数据目录不存在: $DATA_DIR"
fi

# 检查是否运行
db_log_info "检查 openGauss 服务状态..."
if ! db_run_as_user "$OS_USER" "
    export GAUSSHOME=${INSTALL_PATH}
    export PATH=\$GAUSSHOME/bin:\$PATH
    gs_ctl status -D '$DATA_DIR' 2>/dev/null | grep -q 'server is running'
"; then
    db_log_info "openGauss 服务未在运行"
    exit $DB_EXIT_SERVICE_STATE
fi

# 停止服务
db_log_info "停止 openGauss 服务 (mode=$MODE)..."
db_run_as_user "$OS_USER" "
    export GAUSSHOME=${INSTALL_PATH}
    export PATH=\$GAUSSHOME/bin:\$PATH
    export LD_LIBRARY_PATH=\$GAUSSHOME/lib:\$LD_LIBRARY_PATH
    gs_ctl stop -D '$DATA_DIR' -m '$MODE'
" || {
    db_exit_with_error $DB_EXIT_GENERIC_ERROR "停止 openGauss 服务失败"
}

db_log_info "openGauss 服务已停止"
exit $DB_EXIT_SUCCESS
