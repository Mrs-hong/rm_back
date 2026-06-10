#!/bin/bash
# =============================================================================
# MariaDB 初始化脚本
# =============================================================================
# 说明：
#   初始化 MariaDB 数据库实例，包括数据目录初始化、配置修改、root 密码设置、
#   服务启动和开机自启设置。
#
#   注意：MariaDB 通过 apt 安装后会自动在默认路径生成数据。若需按自定义参数
#   重新初始化，请使用 --force 参数。
#
# 用法：
#   init.sh [选项]
#
# 选项：
#   --admin_password=PWD   管理员(root)密码（必填）
#   --data=DIR             数据目录（可选，默认 /var/lib/mysql）
#   --port=PORT            监听端口（可选，默认 3306）
#   --ip=IP                绑定 IP（可选，默认 127.0.0.1）
#   --character_set=SET    字符集（可选，默认 utf8mb4）
#   --force                强制重新初始化（会停止服务并清理已有数据）
#   --migrate_dir=DIR      将已有数据迁移到指定目录（可选，配合 --force 使用）
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
DATA_DIR=$(db_arg "data" "/var/lib/mysql")
PORT=$(db_arg "port" "3306")
IP=$(db_arg "ip" "127.0.0.1")
CHARSET=$(db_arg "character_set" "utf8mb4")
FORCE=$(db_arg "force" "")
MIGRATE_DIR=$(db_arg "migrate_dir" "")

# 帮助信息
if [[ -n "$(db_arg "help" "")" || -n "$(db_arg "h" "")" ]]; then
    db_print_help "init.sh" "初始化 MariaDB 数据库实例" \
"  --admin_password=PWD   管理员(root)密码（必填）
  --data=DIR             数据目录（可选，默认 /var/lib/mysql）
  --port=PORT            监听端口（可选，默认 3306）
  --ip=IP                绑定 IP（可选，默认 127.0.0.1）
  --character_set=SET    字符集（可选，默认 utf8mb4）
  --force                强制重新初始化（会停止服务并清理已有数据）
  --migrate_dir=DIR      将已有数据迁移到指定目录（可选，配合 --force 使用）
  --help, -h             显示此帮助信息"
    exit $DB_EXIT_SUCCESS
fi

# 必填参数检查
if [[ -z "$ADMIN_PASSWORD" ]]; then
    db_exit_with_error $DB_EXIT_INVALID_ARGS "缺少必填参数: --admin_password"
fi

# 检查 root 权限
db_check_root

# 数据目录路径安全校验
if ! db_validate_path "$DATA_DIR"; then
    db_exit_with_error $DB_EXIT_INVALID_ARGS "数据目录路径不安全: $DATA_DIR（不能为空、/、或包含..）"
fi

# 检查端口占用
if db_check_port_in_use "$PORT"; then
    db_exit_with_error $DB_EXIT_PORT_IN_USE "端口 $PORT 已被占用"
fi

db_log_info "开始初始化 MariaDB..."
db_log_info "数据目录: $DATA_DIR"
db_log_info "端口: $PORT"
db_log_info "绑定 IP: $IP"

# 确保数据目录存在并设置正确权限
mkdir -p "$DATA_DIR"
chown -R mysql:mysql "$DATA_DIR" 2>/dev/null || true

# 查找配置文件目录
MYSQL_CONF_DIR=""
if [[ -d /etc/mysql/mariadb.conf.d ]]; then
    MYSQL_CONF_DIR="/etc/mysql/mariadb.conf.d"
elif [[ -d /etc/my.cnf.d ]]; then
    MYSQL_CONF_DIR="/etc/my.cnf.d"
else
    # 创建自定义配置目录
    MYSQL_CONF_DIR="/etc/mysql/conf.d"
    mkdir -p "$MYSQL_CONF_DIR"
fi

# 确保主配置文件包含 mariadb.conf.d 目录
# 某些环境卸载重装后 my.cnf 可能为空，或包含不存在的 conf.d include，导致 drop-in 配置不生效
MY_CNF="/etc/mysql/my.cnf"
NEEDS_FIX=false
if [[ ! -f "$MY_CNF" ]]; then
    NEEDS_FIX=true
elif grep -q "conf.d" "$MY_CNF" 2>/dev/null && [[ ! -d /etc/mysql/conf.d ]]; then
    NEEDS_FIX=true
elif ! grep -q "mariadb.conf.d" "$MY_CNF" 2>/dev/null; then
    NEEDS_FIX=true
fi

if [[ "$NEEDS_FIX" == "true" ]]; then
    db_log_info "修复 $MY_CNF，添加 mariadb.conf.d 包含路径..."
    mkdir -p /etc/mysql/conf.d 2>/dev/null || true
    # 保留已有配置，仅追加缺失的 !includedir 指令
    if ! grep -q "mariadb.conf.d" "$MY_CNF" 2>/dev/null; then
        echo "!includedir /etc/mysql/mariadb.conf.d/" >> "$MY_CNF"
    fi
    if [[ -d /etc/mysql/conf.d ]] && ! grep -q "conf.d" "$MY_CNF" 2>/dev/null; then
        echo "!includedir /etc/mysql/conf.d/" >> "$MY_CNF"
    fi
fi

# 生成自定义配置文件
db_log_info "写入配置文件到 $MYSQL_CONF_DIR/99-custom.cnf"
cat > "$MYSQL_CONF_DIR/99-custom.cnf" <<EOF
[mysqld]
datadir = $DATA_DIR
port = $PORT
bind-address = $IP
character-set-server = $CHARSET

[client]
port = $PORT
default-character-set = $CHARSET
EOF

# 如果使用自定义数据目录，确保 mysql 用户可以穿越父目录并创建 systemd override
if [[ "$DATA_DIR" != "/var/lib/mysql" ]]; then
    # 确保 mysql 用户对数据目录的每一级父目录有穿越(x)权限
    # 否则服务以 mysql 用户运行时无法到达数据目录（如 /home/user/data 中的 /home/user 可能权限不足）
    db_log_info "检查自定义数据目录的父路径权限..."
    _path="/"
    IFS='/' read -ra _parts <<< "${DATA_DIR#/}"
    for _part in "${_parts[@]}"; do
        _path="${_path}${_part}/"
        if [[ -d "$_path" ]]; then
            _perms=$(stat -c '%a' "$_path" 2>/dev/null)
            # 检查 others 是否有 x 权限（最后一位包含 1 或 3 或 5 或 7）
            if [[ -n "$_perms" ]] && [[ $(( ${_perms: -1} & 1 )) -eq 0 ]]; then
                db_log_info "为目录 $_path 添加 others 执行权限（mysql 用户需穿越此目录）"
                chmod o+x "$_path" || {
                    db_log_warn "无法为 $_path 添加执行权限，mysql 用户可能无法访问数据目录"
                }
            fi
        fi
    done
    unset _path _parts _part _perms

    # 创建 systemd override 以允许访问自定义路径
    if db_command_exists systemctl; then
        SYSTEMD_OVERRIDE_DIR="/etc/systemd/system/mariadb.service.d"
        mkdir -p "$SYSTEMD_OVERRIDE_DIR"
        db_log_info "创建 systemd override 配置以支持自定义数据目录..."
        cat > "$SYSTEMD_OVERRIDE_DIR/custom-datadir.conf" <<EOF
[Service]
ProtectHome=false
ReadWritePaths=$DATA_DIR
EOF
        systemctl daemon-reload
    fi
fi

# 初始化数据目录逻辑
DATA_HAS_CONTENT=false
if db_has_valid_data "$DATA_DIR"; then
    DATA_HAS_CONTENT=true
fi

# 若指定了新的非默认路径且新路径为空，尝试从默认路径迁移已有数据
DEFAULT_DATA_DIR="/var/lib/mysql"
if [[ "$DATA_DIR" != "$DEFAULT_DATA_DIR" && "$DATA_HAS_CONTENT" == "false" ]] \
    && db_has_valid_data "$DEFAULT_DATA_DIR"; then
    db_log_info "检测到默认路径 $DEFAULT_DATA_DIR 有有效数据，迁移到 $DATA_DIR ..."

    # 1. 停止服务
    db_log_info "停止 MariaDB 服务..."
    if db_command_exists systemctl; then
        systemctl stop mariadb 2>/dev/null || systemctl stop mysql 2>/dev/null || true
    else
        service mysql stop 2>/dev/null || service mariadb stop 2>/dev/null || true
    fi

    # 2. 迁移数据
    mkdir -p "$DATA_DIR"
    cp -a "$DEFAULT_DATA_DIR"/. "$DATA_DIR"/ || {
        db_exit_with_error $DB_EXIT_INIT_FAILED "迁移数据从 $DEFAULT_DATA_DIR 到 $DATA_DIR 失败"
    }
    chown -R mysql:mysql "$DATA_DIR" || {
        db_log_warn "无法将数据目录属主改为 mysql，可能在容器/受限环境中运行"
    }
    # 清理迁移带来的残留运行时文件，避免服务启动冲突
    rm -f "$DATA_DIR"/*.pid "$DATA_DIR"/*.sock "$DATA_DIR"/*.sock.lock 2>/dev/null || true
    DATA_HAS_CONTENT=true
fi

if [[ "$DATA_HAS_CONTENT" == "true" && -n "$FORCE" ]]; then
    # 强制重新初始化：停止服务 -> 迁移/清理 -> 重新初始化
    db_log_info "检测到 --force，准备强制重新初始化..."

    # 1. 停止服务
    db_log_info "停止 MariaDB 服务..."
    if db_command_exists systemctl; then
        systemctl stop mariadb 2>/dev/null || systemctl stop mysql 2>/dev/null || true
    else
        service mysql stop 2>/dev/null || service mariadb stop 2>/dev/null || true
    fi

    # 2. 处理旧数据：迁移到指定路径，或直接删除
    if [[ -n "$MIGRATE_DIR" ]]; then
        mkdir -p "$(dirname "$MIGRATE_DIR")"
        db_log_info "迁移旧数据到: $MIGRATE_DIR"
        mv "$DATA_DIR" "$MIGRATE_DIR" || {
            db_exit_with_error $DB_EXIT_INIT_FAILED "迁移旧数据目录失败"
        }
    else
        db_log_info "删除旧数据目录: $DATA_DIR"
        db_safe_rm "$DATA_DIR" "旧数据目录" || db_exit_with_error $DB_EXIT_INIT_FAILED "删除旧数据目录失败（路径不安全）"
    fi

    # 3. 重新创建空数据目录
    mkdir -p "$DATA_DIR"
    chown -R mysql:mysql "$DATA_DIR" || {
        db_log_warn "无法将数据目录属主改为 mysql，可能在容器/受限环境中运行"
    }
    DATA_HAS_CONTENT=false
fi

if [[ "$DATA_HAS_CONTENT" == "false" ]]; then
    db_log_info "数据目录为空，执行数据库初始化..."

    # 判断是否为默认数据目录
    if [[ "$DATA_DIR" == "/var/lib/mysql" ]]; then
        # 默认目录：以 mysql 用户初始化
        if db_command_exists mariadb-install-db; then
            mariadb-install-db --user=mysql --datadir="$DATA_DIR" || {
                db_exit_with_error $DB_EXIT_INIT_FAILED "mariadb-install-db 初始化失败"
            }
        elif db_command_exists mysql_install_db; then
            mysql_install_db --user=mysql --datadir="$DATA_DIR" || {
                db_exit_with_error $DB_EXIT_INIT_FAILED "mysql_install_db 初始化失败"
            }
        else
            db_exit_with_error $DB_EXIT_MISSING_DEPEND "未找到 mariadb-install-db 或 mysql_install_db"
        fi
    else
        # 自定义目录：先以 root 初始化（避免 mysql 用户因父目录权限不足无法写入），再改属主
        db_log_info "自定义数据目录，以 root 执行初始化后调整权限..."
        if db_command_exists mariadb-install-db; then
            mariadb-install-db --datadir="$DATA_DIR" || {
                db_exit_with_error $DB_EXIT_INIT_FAILED "mariadb-install-db 初始化失败"
            }
        elif db_command_exists mysql_install_db; then
            mysql_install_db --datadir="$DATA_DIR" || {
                db_exit_with_error $DB_EXIT_INIT_FAILED "mysql_install_db 初始化失败"
            }
        else
            db_exit_with_error $DB_EXIT_MISSING_DEPEND "未找到 mariadb-install-db 或 mysql_install_db"
        fi
        # 初始化完成后将数据目录属主改为 mysql
        chown -R mysql:mysql "$DATA_DIR" || {
            db_log_warn "无法将数据目录属主改为 mysql，可能在容器/受限环境中运行"
        }
    fi
else
    db_log_info "数据目录已存在数据，跳过初始化。如需重新初始化，请使用 --force 参数"
fi

# 启动 MariaDB 服务
db_log_info "启动 MariaDB 服务..."

# 确保数据目录权限正确（chown 可能在迁移时失败，启动前再次尝试）
chown -R mysql:mysql "$DATA_DIR" 2>/dev/null || true

if db_command_exists systemctl; then
    if ! systemctl start mariadb 2>&1 && ! systemctl start mysql 2>&1; then
        # systemctl start 可能返回错误码但服务仍在后台启动成功，等待后检查实际状态
        sleep 3
        if systemctl is-active --quiet mariadb 2>/dev/null || systemctl is-active --quiet mysql 2>/dev/null; then
            db_log_info "服务已在运行，继续执行..."
        else
            # 输出服务日志辅助诊断
            db_log_error "MariaDB 服务启动失败，最近日志："
            journalctl -u mariadb -n 10 --no-pager 2>/dev/null || journalctl -u mysql -n 10 --no-pager 2>/dev/null || true
            db_exit_with_error $DB_EXIT_INIT_FAILED "启动 MariaDB 服务失败"
        fi
    fi
    systemctl enable mariadb 2>/dev/null || systemctl enable mysql 2>/dev/null || true
else
    if ! service mysql start 2>&1 && ! service mariadb start 2>&1; then
        db_exit_with_error $DB_EXIT_INIT_FAILED "启动 MariaDB 服务失败"
    fi
fi

# 等待服务就绪
db_log_info "等待 MariaDB 服务就绪..."
if ! db_wait_for_mysql 30 1; then
    db_exit_with_error $DB_EXIT_INIT_FAILED "等待 MariaDB 服务就绪超时"
fi

# 设置 root 密码
db_log_info "设置 root 密码..."

ESCAPED_PASSWORD=$(db_sql_escape "$ADMIN_PASSWORD")

# 首次尝试：通过 socket 连接（新安装默认使用 unix_socket 认证，无需密码）
# 使用 CREATE USER IF NOT EXISTS 确保两个 host 条目都存在后再修改密码
mysql -u root -e \
    "CREATE USER IF NOT EXISTS 'root'@'127.0.0.1' IDENTIFIED BY '${ESCAPED_PASSWORD}';
     ALTER USER 'root'@'localhost' IDENTIFIED BY '${ESCAPED_PASSWORD}';
     ALTER USER 'root'@'127.0.0.1' IDENTIFIED BY '${ESCAPED_PASSWORD}';
     FLUSH PRIVILEGES;" 2>&1 || {
    # 第二次尝试：通过 TCP 连接（适用于已有密码的场景）
    MYSQL_SET_CNF=$(db_create_mysql_cnf "root" "$ADMIN_PASSWORD" "127.0.0.1" "$PORT")
    mysql --defaults-extra-file="$MYSQL_SET_CNF" -e \
        "CREATE USER IF NOT EXISTS 'root'@'127.0.0.1' IDENTIFIED BY '${ESCAPED_PASSWORD}';
         ALTER USER 'root'@'localhost' IDENTIFIED BY '${ESCAPED_PASSWORD}';
         ALTER USER 'root'@'127.0.0.1' IDENTIFIED BY '${ESCAPED_PASSWORD}';
         FLUSH PRIVILEGES;" 2>&1 || {
        rm -f "$MYSQL_SET_CNF"
        db_exit_with_error $DB_EXIT_INIT_FAILED "设置 root 密码失败"
    }
    rm -f "$MYSQL_SET_CNF"
}

# 验证连接（使用 socket 连接，通过临时配置文件安全传递密码）
db_log_info "验证 root 连接..."
MYSQL_VERIFY_CNF=$(db_create_mysql_cnf "root" "$ADMIN_PASSWORD")
mysql --defaults-extra-file="$MYSQL_VERIFY_CNF" -e "SELECT 1;" >/dev/null 2>&1 || {
    rm -f "$MYSQL_VERIFY_CNF"
    db_exit_with_error $DB_EXIT_INIT_FAILED "使用新密码连接 MariaDB 失败"
}
rm -f "$MYSQL_VERIFY_CNF"

db_log_info "MariaDB 初始化完成"
exit $DB_EXIT_SUCCESS
