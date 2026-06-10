#!/bin/bash
# =============================================================================
# MariaDB 安装脚本
# =============================================================================
# 说明：
#   检查当前系统是否安装了 MariaDB，若未安装则尝试通过 apt 或本地 deb 包安装。
#   支持 Ubuntu/Debian 系统，支持 x86_64 和 aarch64 架构。
#
# 用法：
#   install.sh [选项]
#
# 选项：
#   --version=VERSION    目标版本，如 10.6.33（可选，默认使用系统源最新版）
#   --deb_path=PATH      本地 deb 包路径（可选，若系统源无指定版本则使用）
#   --arch=ARCH          强制指定架构 x86_64|aarch64（可选，默认自动检测）
#
# 返回值：
#   0 成功
#   2 参数错误
#   3 权限不足
#   4 依赖缺失/安装包不存在
#   6 端口被占用（若安装后默认端口 3306 被占）
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../common.sh
source "${SCRIPT_DIR}/../common.sh"

# 解析参数
db_parse_args "$@"

VERSION=$(db_arg "version" "")
DEB_PATH=$(db_arg "deb_path" "")
ARCH=$(db_arg "arch" "")

# 帮助信息
if [[ -n "$(db_arg "help" "")" || -n "$(db_arg "h" "")" ]]; then
    db_print_help "install.sh" "安装 MariaDB 数据库服务" \
"  --version=VERSION    目标版本，如 10.6.33（可选，默认使用系统源最新版）
  --deb_path=PATH      本地 deb 包路径（可选）
  --arch=ARCH          强制指定架构 x86_64|aarch64（可选，默认自动检测）
  --help, -h           显示此帮助信息"
    exit $DB_EXIT_SUCCESS
fi

# 自动检测架构
if [[ -z "$ARCH" ]]; then
    ARCH=$(db_check_arch)
fi
db_log_info "当前系统架构: $ARCH"

# 检查 root 权限
db_check_root

# 检测操作系统
OS=$(db_check_os)
db_log_info "当前操作系统: $OS"

if [[ "$OS" != "ubuntu" && "$OS" != "debian" ]]; then
    db_log_warn "当前脚本主要为 Ubuntu/Debian 设计，当前 OS: $OS"
fi

# 检查 MariaDB 是否已安装且可用
# 仅当命令确实存在且可执行时才跳过，避免 dpkg 状态损坏时误跳
db_log_info "检查 MariaDB 是否已安装..."
if db_command_exists mariadb || db_command_exists mysqld; then
    db_log_info "MariaDB 已安装且可用，跳过安装步骤"
    exit $DB_EXIT_SUCCESS
fi

# 修复可能的 dpkg 损坏状态（如上次卸载中断、配置缺失等），保证幂等性
db_log_info "修复可能的包管理器状态..."
if ! dpkg --configure -a 2>&1; then
    db_log_warn "dpkg --configure -a 执行失败，继续安装..."
fi
if ! apt-get install -f -y 2>&1; then
    db_log_warn "apt-get install -f 执行失败，继续安装..."
fi

# 检查默认端口 3306 是否被占用（仅作为警告）
if db_check_port_in_use 3306; then
    db_log_info "警告: 端口 3306 已被占用，安装后可能需要调整配置"
fi

# 安装逻辑
db_log_info "开始安装 MariaDB..."

if [[ -n "$DEB_PATH" ]]; then
    # 使用本地 deb 包安装
    db_log_info "使用本地 deb 包安装: $DEB_PATH"
    if [[ ! -f "$DEB_PATH" ]]; then
        db_exit_with_error $DB_EXIT_MISSING_DEPEND "本地 deb 包不存在: $DEB_PATH"
    fi

    dpkg -i "$DEB_PATH" || true
    apt-get install -f -y || db_exit_with_error $DB_EXIT_MISSING_DEPEND "安装依赖失败"
else
    # 使用 apt 安装
    apt-get update || db_exit_with_error $DB_EXIT_GENERIC_ERROR "apt-get update 失败"

    if [[ -n "$VERSION" ]]; then
        db_log_info "尝试安装指定版本: $VERSION"
        # 先检查 apt 是否有该版本
        if apt-cache madison mariadb-server 2>/dev/null | grep -qE "[|] ${VERSION}[-+|]"; then
            apt-get install -y "mariadb-server=$VERSION" "mariadb-client=$VERSION" || {
                db_exit_with_error $DB_EXIT_MISSING_DEPEND "apt 安装 MariaDB $VERSION 失败"
            }
        else
            db_exit_with_error $DB_EXIT_MISSING_DEPEND "apt 源中未找到 MariaDB 版本 $VERSION，请提供 --deb_path"
        fi
    else
        db_log_info "使用系统源安装最新版 MariaDB"
        apt-get install -y mariadb-server mariadb-client || {
            db_exit_with_error $DB_EXIT_MISSING_DEPEND "apt 安装 MariaDB 失败"
        }
    fi
fi

# 确保默认数据目录已正确初始化
# apt postinst 在某些受限环境或 dpkg 状态异常时可能只创建标记文件而未真正初始化
DEFAULT_DATA_DIR="/var/lib/mysql"
if ! db_has_valid_data "$DEFAULT_DATA_DIR"; then
    db_log_info "默认数据目录未初始化，补充执行初始化..."
    mkdir -p "$DEFAULT_DATA_DIR"
    chown mysql:mysql "$DEFAULT_DATA_DIR" 2>/dev/null || true
    if db_command_exists mariadb-install-db; then
        mariadb-install-db --user=mysql --datadir="$DEFAULT_DATA_DIR" || {
            db_log_warn "以 mysql 用户初始化失败，尝试以 root 初始化..."
            mariadb-install-db --datadir="$DEFAULT_DATA_DIR" || {
                db_log_warn "默认数据目录初始化失败，将在 init.sh 中处理"
            }
        }
    elif db_command_exists mysql_install_db; then
        mysql_install_db --user=mysql --datadir="$DEFAULT_DATA_DIR" || {
            db_log_warn "以 mysql 用户初始化失败，尝试以 root 初始化..."
            mysql_install_db --datadir="$DEFAULT_DATA_DIR" || {
                db_log_warn "默认数据目录初始化失败，将在 init.sh 中处理"
            }
        }
    fi
    # 初始化完成后调整权限
    chown -R mysql:mysql "$DEFAULT_DATA_DIR" 2>/dev/null || true
fi

# 验证安装
if db_command_exists mariadb || db_command_exists mysqld; then
    db_log_info "MariaDB 安装成功"
    exit $DB_EXIT_SUCCESS
else
    db_exit_with_error $DB_EXIT_GENERIC_ERROR "MariaDB 安装后验证失败"
fi
