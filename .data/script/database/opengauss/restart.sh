#!/bin/bash
# =============================================================================
# openGauss 重启脚本
# =============================================================================
# 说明：
#   重启 openGauss 数据库服务。
#
# 用法：
#   restart.sh [选项]
#
# 选项：
#   --data=DIR          数据目录（可选，默认 /var/lib/opengauss/data）
#   --user=OS_USER      运行用户（可选，默认 omm）
#   --install_path=DIR  安装目录（可选，默认 /opt/opengauss）
#   --mode=MODE         停止模式：smart|fast|immediate（可选，默认 fast）
#   --help, -h          显示此帮助信息
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
    db_print_help "restart.sh" "重启 openGauss 数据库服务" \
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

# 先停止（无论是否运行都尝试）
db_log_info "停止 openGauss 服务..."
db_run_as_user "$OS_USER" "
    export GAUSSHOME=${INSTALL_PATH}
    export PATH=\$GAUSSHOME/bin:\$PATH
    gs_ctl stop -D '$DATA_DIR' -m '$MODE' 2>/dev/null || true
" || true

sleep 2

# 启动
db_log_info "启动 openGauss 服务..."
db_run_as_user "$OS_USER" "
    export GAUSSHOME=${INSTALL_PATH}
    export PATH=\$GAUSSHOME/bin:\$PATH
    export LD_LIBRARY_PATH=\$GAUSSHOME/lib:\$LD_LIBRARY_PATH
    gs_ctl start -D '$DATA_DIR' -l '$DATA_DIR/log/opengauss.log'
" || {
    db_exit_with_error $DB_EXIT_GENERIC_ERROR "重启 openGauss 服务失败"
}

db_log_info "openGauss 服务重启成功"
exit $DB_EXIT_SUCCESS
