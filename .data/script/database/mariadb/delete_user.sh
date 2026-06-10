#!/bin/bash
# =============================================================================
# MariaDB 删除用户及所属数据库
# =============================================================================
# 说明：
#   使用管理员账号连接 MariaDB，删除指定用户及其拥有的所有数据库。
#   通过查询 information_schema 识别用户拥有的数据库，然后逐一删除。
#
# 用法：
#   delete_user.sh [选项]
#
# 选项：
#   --admin_user=USER       管理员用户名（可选，默认 root）
#   --admin_password=PWD    管理员密码（必填）
#   --user=USER             待删除的用户名（必填）
#   --host=HOST             连接地址（可选，默认 127.0.0.1）
#   --port=PORT             连接端口（可选，默认 3306）
#   --skip_db               仅删除用户，不删除数据库（可选）
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
HOST=$(db_arg "host" "127.0.0.1")
PORT=$(db_arg "port" "3306")
SKIP_DB=$(db_arg "skip_db" "")

# 帮助信息
if [[ -n "$(db_arg "help" "")" || -n "$(db_arg "h" "")" ]]; then
    db_print_help "delete_user.sh" "删除 MariaDB 用户及其所属数据库" \
"  --admin_user=USER       管理员用户名（可选，默认 root）
  --admin_password=PWD    管理员密码（必填）
  --user=USER             待删除的用户名（必填）
  --host=HOST             连接地址（可选，默认 127.0.0.1）
  --port=PORT             连接端口（可选，默认 3306）
  --skip_db               仅删除用户，不删除数据库（可选）
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

# SQL 转义用户名
ESCAPED_USER=$(db_sql_escape "$USER")

# 创建管理员临时配置文件
ADMIN_CNF=$(db_create_mysql_cnf "$ADMIN_USER" "$ADMIN_PASSWORD" "$HOST" "$PORT")

# 测试管理员连接
db_log_info "测试管理员连接..."
mysql --defaults-extra-file="$ADMIN_CNF" -e "SELECT 1;" >/dev/null 2>&1 || {
    rm -f "$ADMIN_CNF"
    db_exit_with_error $DB_EXIT_DB_CONNECT_FAILED "管理员连接失败，请检查密码、主机和端口"
}

# 检查用户是否存在
db_log_info "检查用户 $USER 是否存在..."
USER_HOSTS=$(mysql --defaults-extra-file="$ADMIN_CNF" -N -e \
    "SELECT CONCAT(QUOTE(user), '@', QUOTE(host)) FROM mysql.user WHERE user = '${ESCAPED_USER}';" 2>/dev/null)

if [[ -z "$USER_HOSTS" ]]; then
    db_log_warn "用户 $USER 不存在，跳过删除"
    rm -f "$ADMIN_CNF"
    exit $DB_EXIT_SUCCESS
fi

db_log_info "找到用户条目: $USER_HOSTS"

# 删除用户拥有的数据库（除非指定 --skip_db）
if [[ -z "$SKIP_DB" ]]; then
    db_log_info "查询用户 $USER 拥有的数据库..."

    # 查询该用户有独占 ALL PRIVILEGES 权限的数据库
    # 排除系统数据库（mysql, information_schema, performance_schema, sys）
    OWNED_DBS=$(mysql --defaults-extra-file="$ADMIN_CNF" -N -e "
        SELECT DISTINCT t.table_schema
        FROM information_schema.tables t
        WHERE t.table_schema NOT IN ('mysql', 'information_schema', 'performance_schema', 'sys')
          AND EXISTS (
            SELECT 1 FROM information_schema.schema_privileges sp
            WHERE sp.grantee IN ('''${ESCAPED_USER}''@''%''', '''${ESCAPED_USER}''@''localhost''')
              AND sp.table_schema = t.table_schema
              AND sp.privilege_type = 'ALL PRIVILEGES'
          );
    " 2>/dev/null)

    if [[ -n "$OWNED_DBS" ]]; then
        db_log_info "用户 $USER 拥有以下数据库:"
        for db in $OWNED_DBS; do
            db_log_info "  - $db"
        done

        for db in $OWNED_DBS; do
            ESCAPED_DB=$(db_sql_escape "$db")
            db_log_info "删除数据库: $db"
            mysql --defaults-extra-file="$ADMIN_CNF" -e "DROP DATABASE IF EXISTS \`${ESCAPED_DB//\`/\`\`}\`;" || {
                db_log_warn "删除数据库 $db 失败，继续执行..."
            }
        done
    else
        db_log_info "未找到用户 $USER 拥有的数据库"
    fi
else
    db_log_info "指定了 --skip_db，跳过数据库删除"
fi

# 撤销权限并删除用户
db_log_info "撤销用户 $USER 的权限..."
for user_host in $USER_HOSTS; do
    mysql --defaults-extra-file="$ADMIN_CNF" -e "REVOKE ALL PRIVILEGES, GRANT OPTION FROM $user_host;" 2>/dev/null || true
done

db_log_info "删除用户 $USER..."
for user_host in $USER_HOSTS; do
    mysql --defaults-extra-file="$ADMIN_CNF" -e "DROP USER $user_host;" || {
        db_log_warn "删除用户 $user_host 失败"
    }
done

mysql --defaults-extra-file="$ADMIN_CNF" -e "FLUSH PRIVILEGES;"

rm -f "$ADMIN_CNF"
db_log_info "用户 $USER 及其所属数据已删除"
exit $DB_EXIT_SUCCESS
