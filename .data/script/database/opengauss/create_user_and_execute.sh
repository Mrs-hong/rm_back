#!/bin/bash
# =============================================================================
# openGauss 创建用户并执行 SQL 脚本
# =============================================================================
# 说明：
#   使用管理员账号连接 openGauss，创建新用户并授权，然后可选执行指定目录下的 SQL 文件。
#
# 用法：
#   create_user_and_execute.sh [选项]
#
# 选项：
#   --admin_user=USER       管理员用户名（可选，默认 omm）
#   --admin_password=PWD    管理员密码（必填）
#   --user=USER             待创建的数据库用户名（必填）
#   --password=PWD          待创建用户的密码（必填）
#   --sql_dir=DIR           SQL 脚本所在目录（可选，按文件名排序执行所有 .sql 文件）
#   --host=HOST             连接地址（可选，默认 127.0.0.1）
#   --port=PORT             连接端口（可选，默认 5432）
#   --database=DB           连接的数据库（可选，默认 postgres）
#   --install_path=DIR      安装目录（可选，默认 /opt/opengauss）
#   --help, -h              显示此帮助信息
#
# 返回值：
#   0 成功
#   2 参数错误
#   8 数据库连接失败
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../common.sh
source "${SCRIPT_DIR}/../common.sh"

# 解析参数
db_parse_args "$@"

ADMIN_USER=$(db_arg "admin_user" "omm")
ADMIN_PASSWORD=$(db_arg "admin_password" "")
USER=$(db_arg "user" "")
PASSWORD=$(db_arg "password" "")
SQL_DIR=$(db_arg "sql_dir" "")
HOST=$(db_arg "host" "127.0.0.1")
PORT=$(db_arg "port" "5432")
DATABASE=$(db_arg "database" "postgres")
INSTALL_PATH=$(db_arg "install_path" "/opt/opengauss")

# 帮助信息
if [[ -n "$(db_arg "help" "")" || -n "$(db_arg "h" "")" ]]; then
    db_print_help "create_user_and_execute.sh" "创建 openGauss 用户并执行 SQL 脚本" \
"  --admin_user=USER       管理员用户名（可选，默认 omm）
  --admin_password=PWD    管理员密码（必填）
  --user=USER             待创建的数据库用户名（必填）
  --password=PWD          待创建用户的密码（必填）
  --sql_dir=DIR           SQL 脚本所在目录（可选）
  --host=HOST             连接地址（可选，默认 127.0.0.1）
  --port=PORT             连接端口（可选，默认 5432）
  --database=DB           连接的数据库（可选，默认 postgres）
  --install_path=DIR      安装目录（可选，默认 /opt/opengauss）
  --help, -h              显示此帮助信息"
    exit $DB_EXIT_SUCCESS
fi

# 必填参数检查
if [[ -z "$ADMIN_PASSWORD" ]]; then
    db_exit_with_error $DB_EXIT_INVALID_ARGS "缺少必填参数: --admin_password"
fi
if [[ -z "$USER" ]]; then
    db_exit_with_error $DB_EXIT_INVALID_ARGS "缺少必填参数: --user"
fi
if [[ -z "$PASSWORD" ]]; then
    db_exit_with_error $DB_EXIT_INVALID_ARGS "缺少必填参数: --password"
fi

# 构建 gsql 命令
GSQL_OPTS="-d ${DATABASE} -p ${PORT} -h ${HOST} -U ${ADMIN_USER} -W '${ADMIN_PASSWORD}'"

# 测试管理员连接
db_log_info "测试管理员连接..."
if ! db_run_as_user "$ADMIN_USER" "
    export GAUSSHOME=${INSTALL_PATH}
    export PATH=\$GAUSSHOME/bin:\$PATH
    gsql ${GSQL_OPTS} -c 'SELECT 1;' >/dev/null 2>&1
" 2>/dev/null; then
    db_exit_with_error $DB_EXIT_DB_CONNECT_FAILED "管理员连接失败，请检查密码、主机和端口"
fi

# 执行 gsql 的辅助函数
run_gsql() {
    local sql="$1"
    db_run_as_user "$ADMIN_USER" "
        export GAUSSHOME=${INSTALL_PATH}
        export PATH=\$GAUSSHOME/bin:\$PATH
        gsql ${GSQL_OPTS} -c \"$sql\"
    "
}

# 创建用户并授权
db_log_info "创建用户: $USER"
run_gsql "CREATE USER ${USER} WITH PASSWORD '${PASSWORD}';" 2>/dev/null || {
    db_log_info "用户可能已存在，尝试修改密码..."
    run_gsql "ALTER USER ${USER} WITH PASSWORD '${PASSWORD}';" || {
        db_exit_with_error $DB_EXIT_GENERIC_ERROR "创建或修改用户失败"
    }
}

# 授权（授予所有数据库的权限）
run_gsql "GRANT ALL PRIVILEGES TO ${USER};" 2>/dev/null || true

db_log_info "用户 $USER 创建/更新成功"

# 执行 SQL 文件
if [[ -n "$SQL_DIR" ]]; then
    if [[ ! -d "$SQL_DIR" ]]; then
        db_exit_with_error $DB_EXIT_INVALID_ARGS "SQL 目录不存在: $SQL_DIR"
    fi

    db_log_info "执行 SQL 目录: $SQL_DIR"
    count=0
    for sql_file in $(ls -1 "$SQL_DIR"/*.sql 2>/dev/null | sort); do
        db_log_info "执行 SQL 文件: $sql_file"
        db_run_as_user "$ADMIN_USER" "
            export GAUSSHOME=${INSTALL_PATH}
            export PATH=\$GAUSSHOME/bin:\$PATH
            gsql ${GSQL_OPTS} -f '$sql_file'
        " || {
            db_exit_with_error $DB_EXIT_GENERIC_ERROR "执行 SQL 文件失败: $sql_file"
        }
        count=$((count + 1))
    done
    db_log_info "共执行 $count 个 SQL 文件"
fi

db_log_info "操作完成"
exit $DB_EXIT_SUCCESS
