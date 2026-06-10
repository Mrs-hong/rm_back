#!/bin/bash
# =============================================================================
# MariaDB 创建用户并执行 SQL 脚本
# =============================================================================
# 说明：
#   使用管理员账号连接 MariaDB，创建新用户并授权，然后以新用户身份执行 SQL 脚本。
#   SQL 以新用户身份执行，创建的数据库和表归属于该用户，便于后续删除用户时一并清理。
#
# 用法：
#   create_user_and_execute.sh [选项]
#
# 选项：
#   --admin_user=USER       管理员用户名（可选，默认 root）
#   --admin_password=PWD    管理员密码（必填）
#   --user=USER             待创建的用户名（必填）
#   --password=PWD          待创建用户的密码（必填）
#   --sql_dir=DIR           SQL 脚本所在目录（可选，按文件名排序执行所有 .sql 文件）
#   --database=DB           默认数据库（可选，执行 SQL 时使用）
#   --host=HOST             连接地址（可选，默认 127.0.0.1）
#   --port=PORT             连接端口（可选，默认 3306）
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

ADMIN_USER=$(db_arg "admin_user" "root")
ADMIN_PASSWORD=$(db_arg "admin_password" "")
USER=$(db_arg "user" "")
PASSWORD=$(db_arg "password" "")
SQL_DIR=$(db_arg "sql_dir" "")
DATABASE=$(db_arg "database" "")
HOST=$(db_arg "host" "127.0.0.1")
PORT=$(db_arg "port" "3306")

# 帮助信息
if [[ -n "$(db_arg "help" "")" || -n "$(db_arg "h" "")" ]]; then
    db_print_help "create_user_and_execute.sh" "创建 MariaDB 用户并以该用户身份执行 SQL 脚本" \
"  --admin_user=USER       管理员用户名（可选，默认 root）
  --admin_password=PWD    管理员密码（必填）
  --user=USER             待创建的用户名（必填）
  --password=PWD          待创建用户的密码（必填）
  --sql_dir=DIR           SQL 脚本所在目录（可选，按文件名排序执行）
  --database=DB           默认数据库（可选，执行 SQL 时使用）
  --host=HOST             连接地址（可选，默认 127.0.0.1）
  --port=PORT             连接端口（可选，默认 3306）
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

# SQL 转义用户名和密码
ESCAPED_USER=$(db_sql_escape "$USER")
ESCAPED_PASSWORD=$(db_sql_escape "$PASSWORD")

# 创建管理员临时配置文件
ADMIN_CNF=$(db_create_mysql_cnf "$ADMIN_USER" "$ADMIN_PASSWORD" "$HOST" "$PORT")

# 测试管理员连接
db_log_info "测试管理员连接..."
mysql --defaults-extra-file="$ADMIN_CNF" -e "SELECT 1;" >/dev/null 2>&1 || {
    rm -f "$ADMIN_CNF"
    db_exit_with_error $DB_EXIT_DB_CONNECT_FAILED "管理员连接失败，请检查密码、主机和端口"
}

# 创建用户并授权
# 同时创建 '%' 和 'localhost' 两个 host 条目，确保 TCP 和 socket 连接都能匹配
db_log_info "创建用户: $USER"
mysql --defaults-extra-file="$ADMIN_CNF" -e "
CREATE USER IF NOT EXISTS '${ESCAPED_USER}'@'%' IDENTIFIED BY '${ESCAPED_PASSWORD}';
CREATE USER IF NOT EXISTS '${ESCAPED_USER}'@'localhost' IDENTIFIED BY '${ESCAPED_PASSWORD}';
ALTER USER '${ESCAPED_USER}'@'%' IDENTIFIED BY '${ESCAPED_PASSWORD}';
ALTER USER '${ESCAPED_USER}'@'localhost' IDENTIFIED BY '${ESCAPED_PASSWORD}';
GRANT ALL PRIVILEGES ON *.* TO '${ESCAPED_USER}'@'%' WITH GRANT OPTION;
GRANT ALL PRIVILEGES ON *.* TO '${ESCAPED_USER}'@'localhost' WITH GRANT OPTION;
FLUSH PRIVILEGES;
" || {
    rm -f "$ADMIN_CNF"
    db_exit_with_error $DB_EXIT_GENERIC_ERROR "创建用户或授权失败"
}

db_log_info "用户 $USER 创建/更新成功"

# 清理管理员配置文件（后续不再需要）
rm -f "$ADMIN_CNF"

# 执行 SQL 文件（以新用户身份执行，创建的数据库和表归属于该用户）
if [[ -n "$SQL_DIR" ]]; then
    if [[ ! -d "$SQL_DIR" ]]; then
        db_exit_with_error $DB_EXIT_INVALID_ARGS "SQL 目录不存在: $SQL_DIR"
    fi

    # 创建新用户的临时配置文件
    USER_CNF=$(db_create_mysql_cnf "$USER" "$PASSWORD" "$HOST" "$PORT")

    # 验证新用户连接
    db_log_info "验证新用户 $USER 连接..."
    mysql --defaults-extra-file="$USER_CNF" -e "SELECT 1;" >/dev/null 2>&1 || {
        rm -f "$USER_CNF"
        db_exit_with_error $DB_EXIT_DB_CONNECT_FAILED "新用户 $USER 连接失败"
    }

    # 构建 mysql 命令的数据库参数
    MYSQL_DB_OPT=""
    if [[ -n "$DATABASE" ]]; then
        MYSQL_DB_OPT="--database=$DATABASE"
    fi

    db_log_info "以用户 $USER 身份执行 SQL 目录: $SQL_DIR"
    count=0
    for sql_file in $(ls -1 "$SQL_DIR"/*.sql 2>/dev/null | sort); do
        db_log_info "执行 SQL 文件: $sql_file"
        mysql --defaults-extra-file="$USER_CNF" $MYSQL_DB_OPT < "$sql_file" || {
            rm -f "$USER_CNF"
            db_exit_with_error $DB_EXIT_GENERIC_ERROR "执行 SQL 文件失败: $sql_file"
        }
        ((count++))
    done
    db_log_info "共执行 $count 个 SQL 文件"

    rm -f "$USER_CNF"
fi

db_log_info "操作完成"
exit $DB_EXIT_SUCCESS
