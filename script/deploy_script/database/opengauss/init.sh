#!/bin/bash
# =============================================================================
# openGauss 初始化脚本
# =============================================================================
# 说明：
#   初始化 openGauss 数据库实例，包括数据目录初始化、配置修改、服务启动。
#   需要以 root 运行，但初始化命令会通过 su 切换到指定 OS 用户执行。
#
# 用法：
#   init.sh [选项]
#
# 选项：
#   --admin_password=PWD    管理员密码（必填，对应初始用户）
#   --data=DIR              数据目录（可选，默认 /var/lib/opengauss/data）
#   --port=PORT             监听端口（可选，默认 5432）
#   --ip=IP                 监听 IP（可选，默认 127.0.0.1）
#   --user=OS_USER          运行 openGauss 的操作系统用户（可选，默认 omm）
#   --install_path=DIR      安装目录（可选，默认 /opt/opengauss）
#   --help, -h              显示此帮助信息
#
# 返回值：
#   0 成功
#   2 参数错误
#   3 权限不足
#   5 服务状态冲突
#   6 端口被占用
#   7 初始化失败
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../common.sh
source "${SCRIPT_DIR}/../common.sh"

# 解析参数
db_parse_args "$@"

ADMIN_PASSWORD=$(db_arg "admin_password" "")
DATA_DIR=$(db_arg "data" "/var/lib/opengauss/data")
PORT=$(db_arg "port" "5432")
IP=$(db_arg "ip" "127.0.0.1")
OS_USER=$(db_arg "user" "omm")
INSTALL_PATH=$(db_arg "install_path" "/opt/opengauss")

# 帮助信息
if [[ -n "$(db_arg "help" "")" || -n "$(db_arg "h" "")" ]]; then
    db_print_help "init.sh" "初始化 openGauss 数据库实例" \
"  --admin_password=PWD    管理员密码（必填）
  --data=DIR              数据目录（可选，默认 /var/lib/opengauss/data）
  --port=PORT             监听端口（可选，默认 5432）
  --ip=IP                 监听 IP（可选，默认 127.0.0.1）
  --user=OS_USER          运行用户（可选，默认 omm）
  --install_path=DIR      安装目录（可选，默认 /opt/opengauss）
  --help, -h              显示此帮助信息"
    exit $DB_EXIT_SUCCESS
fi

# 必填参数检查
if [[ -z "$ADMIN_PASSWORD" ]]; then
    db_exit_with_error $DB_EXIT_INVALID_ARGS "缺少必填参数: --admin_password"
fi

# 检查 root 权限
db_check_root

# 检查端口占用
if db_check_port_in_use "$PORT"; then
    db_exit_with_error $DB_EXIT_PORT_IN_USE "端口 $PORT 已被占用"
fi

# 检查安装目录
if [[ ! -f "${INSTALL_PATH}/bin/gs_initdb" ]]; then
    db_exit_with_error $DB_EXIT_MISSING_DEPEND "未找到 gs_initdb，请确认 openGauss 已正确安装于 ${INSTALL_PATH}"
fi

# 确保运行用户存在（root 除外）
if [[ "$OS_USER" != "root" ]] && ! id "$OS_USER" >/dev/null 2>&1; then
    db_exit_with_error $DB_EXIT_MISSING_DEPEND "运行用户 $OS_USER 不存在，请先执行 install.sh"
fi

# 创建数据目录并设置权限
mkdir -p "$(dirname "$DATA_DIR")"
chown -R "${OS_USER}:${OS_USER}" "$(dirname "$DATA_DIR")"

db_log_info "开始初始化 openGauss..."
db_log_info "数据目录: $DATA_DIR"
db_log_info "端口: $PORT"
db_log_info "运行用户: $OS_USER"

# 初始化数据库（以 OS_USER 身份执行）
if [[ -z "$(ls -A "$DATA_DIR" 2>/dev/null)" ]]; then
    db_log_info "数据目录为空，执行 gs_initdb..."
    db_run_as_user "$OS_USER" "
        export GAUSSHOME=${INSTALL_PATH}
        export PATH=\$GAUSSHOME/bin:\$PATH
        export LD_LIBRARY_PATH=\$GAUSSHOME/lib:\$LD_LIBRARY_PATH
        gs_initdb -D '$DATA_DIR' --nodename='sgnode' -E UTF-8 --locale=en_US.UTF-8 --dbcompatibility=PG
    " || {
        db_exit_with_error $DB_EXIT_INIT_FAILED "gs_initdb 初始化失败"
    }
else
    db_log_info "数据目录已存在数据，跳过初始化"
fi

# 修改配置文件
db_log_info "修改配置文件..."
db_run_as_user "$OS_USER" "
    export GAUSSHOME=${INSTALL_PATH}
    export PATH=\$GAUSSHOME/bin:\$PATH

    # 修改 postgresql.conf
    sed -i \"s/^#*port = .*/port = ${PORT}/\" '$DATA_DIR/postgresql.conf'
    sed -i \"s/^#*listen_addresses = .*/listen_addresses = '${IP}'/\" '$DATA_DIR/postgresql.conf'

    # 修改 pg_hba.conf，允许连接
    echo 'host    all             all             0.0.0.0/0               sha256' >> '$DATA_DIR/pg_hba.conf'
"

# 启动服务
db_log_info "启动 openGauss 服务..."
db_run_as_user "$OS_USER" "
    export GAUSSHOME=${INSTALL_PATH}
    export PATH=\$GAUSSHOME/bin:\$PATH
    export LD_LIBRARY_PATH=\$GAUSSHOME/lib:\$LD_LIBRARY_PATH
    gs_ctl start -D '$DATA_DIR' -l '$DATA_DIR/log/opengauss.log' || exit 1
" || {
    db_exit_with_error $DB_EXIT_INIT_FAILED "启动 openGauss 服务失败"
}

# 等待服务就绪
sleep 3

# 修改初始用户密码
db_log_info "设置管理员密码..."
db_run_as_user "$OS_USER" "
    export GAUSSHOME=${INSTALL_PATH}
    export PATH=\$GAUSSHOME/bin:\$PATH
    gsql -d postgres -p ${PORT} -c \"ALTER USER ${OS_USER} WITH PASSWORD '${ADMIN_PASSWORD}';\" || exit 1
" || {
    db_exit_with_error $DB_EXIT_INIT_FAILED "设置管理员密码失败"
}

# 验证连接
db_log_info "验证连接..."
db_run_as_user "$OS_USER" "
    export GAUSSHOME=${INSTALL_PATH}
    export PATH=\$GAUSSHOME/bin:\$PATH
    gsql -d postgres -p ${PORT} -W '${ADMIN_PASSWORD}' -c 'SELECT 1;' >/dev/null 2>&1 || exit 1
" || {
    db_exit_with_error $DB_EXIT_INIT_FAILED "连接验证失败"
}

db_log_info "openGauss 初始化完成"
exit $DB_EXIT_SUCCESS
