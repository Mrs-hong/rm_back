#!/bin/bash
# =============================================================================
# openGauss 安装脚本
# =============================================================================
# 说明：
#   安装 openGauss 数据库。支持通过本地 tar.gz 包安装，或尝试从官方仓库下载。
#   会自动创建运行用户（如 omm）并设置目录权限。
#   支持 x86_64 和 aarch64 架构。
#
# 用法：
#   install.sh [选项]
#
# 选项：
#   --version=VERSION       目标版本，如 5.0.0（可选）
#   --tar_path=PATH         本地 tar.gz 包路径（可选）
#   --arch=ARCH             强制指定架构 x86_64|aarch64（可选，默认自动检测）
#   --install_path=DIR      安装目录（可选，默认 /opt/opengauss）
#   --user=OS_USER          运行用户（可选，默认 omm）
#   --help, -h              显示此帮助信息
#
# 返回值：
#   0 成功
#   2 参数错误
#   3 权限不足
#   4 依赖缺失/安装包不存在
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../common.sh
source "${SCRIPT_DIR}/../common.sh"

# 解析参数
db_parse_args "$@"

VERSION=$(db_arg "version" "")
TAR_PATH=$(db_arg "tar_path" "")
ARCH=$(db_arg "arch" "")
INSTALL_PATH=$(db_arg "install_path" "/opt/opengauss")
OS_USER=$(db_arg "user" "omm")

# 帮助信息
if [[ -n "$(db_arg "help" "")" || -n "$(db_arg "h" "")" ]]; then
    db_print_help "install.sh" "安装 openGauss 数据库" \
"  --version=VERSION       目标版本，如 5.0.0（可选）
  --tar_path=PATH         本地 tar.gz 包路径（可选）
  --arch=ARCH             强制指定架构 x86_64|aarch64（可选，默认自动检测）
  --install_path=DIR      安装目录（可选，默认 /opt/opengauss）
  --user=OS_USER          运行用户（可选，默认 omm）
  --help, -h              显示此帮助信息"
    exit $DB_EXIT_SUCCESS
fi

# 自动检测架构
if [[ -z "$ARCH" ]]; then
    ARCH=$(db_check_arch)
fi
db_log_info "当前系统架构: $ARCH"

# 检查 root 权限
db_check_root

# 检查是否已安装
db_log_info "检查 openGauss 是否已安装..."
if [[ -f "${INSTALL_PATH}/bin/gs_ctl" ]] || id "$OS_USER" &>/dev/null; then
    db_log_info "openGauss 似乎已安装（检测到安装目录或用户存在）"
fi

# 创建操作系统用户和组（root 除外）
if [[ "$OS_USER" != "root" ]]; then
    db_log_info "创建运行用户和组: $OS_USER"
    if ! getent group "$OS_USER" >/dev/null 2>&1; then
        groupadd "$OS_USER" || db_exit_with_error $DB_EXIT_GENERIC_ERROR "创建用户组 $OS_USER 失败"
    fi
    if ! id "$OS_USER" >/dev/null 2>&1; then
        useradd -g "$OS_USER" -m -d "/home/$OS_USER" -s /bin/bash "$OS_USER" || {
            db_exit_with_error $DB_EXIT_GENERIC_ERROR "创建用户 $OS_USER 失败"
        }
    fi
fi

# 安装依赖
db_log_info "安装 openGauss 依赖..."
apt-get update >/dev/null 2>&1 || true
apt-get install -y libaio1 libncurses5 libreadline8 curl wget tar gzip 2>/dev/null || true

# 安装逻辑
mkdir -p "$INSTALL_PATH"

if [[ -n "$TAR_PATH" ]]; then
    # 使用本地 tar.gz 包
    db_log_info "使用本地包安装: $TAR_PATH"
    if [[ ! -f "$TAR_PATH" ]]; then
        db_exit_with_error $DB_EXIT_MISSING_DEPEND "本地 tar.gz 包不存在: $TAR_PATH"
    fi
    tar -zxvf "$TAR_PATH" -C "$INSTALL_PATH" --strip-components=1 || {
        db_exit_with_error $DB_EXIT_MISSING_DEPEND "解压 tar.gz 包失败"
    }
else
    db_log_info "未提供本地包，请手动下载对应版本的 openGauss 轻量版 tar.gz 并提供 --tar_path"
    db_log_info "openGauss 官方下载地址参考: https://opengauss.org/zh/download/"
    db_exit_with_error $DB_EXIT_MISSING_DEPEND "未找到本地安装包，请提供 --tar_path"
fi

# 设置目录权限
chown -R "${OS_USER}:${OS_USER}" "$INSTALL_PATH"
chmod -R 755 "$INSTALL_PATH"

# 设置环境变量（写入用户 .bashrc，root 除外）
db_log_info "配置环境变量..."
if [[ "$OS_USER" != "root" ]]; then
    cat >> "/home/${OS_USER}/.bashrc" <<EOF
export GAUSSHOME=${INSTALL_PATH}
export PATH=\$GAUSSHOME/bin:\$PATH
export LD_LIBRARY_PATH=\$GAUSSHOME/lib:\$LD_LIBRARY_PATH
EOF
fi

# 同时创建全局 profile 配置（便于 root 或其他用户使用）
cat > /etc/profile.d/opengauss.sh <<EOF
export GAUSSHOME=${INSTALL_PATH}
export PATH=\$GAUSSHOME/bin:\$PATH
export LD_LIBRARY_PATH=\$GAUSSHOME/lib:\$LD_LIBRARY_PATH
EOF

db_log_info "openGauss 安装完成，安装路径: $INSTALL_PATH"
exit $DB_EXIT_SUCCESS
