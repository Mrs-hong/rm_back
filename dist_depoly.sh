#!/bin/bash
# ============================================================================
# qifeng-scm 就地部署脚本
# 将 dist/ 目录拷贝到目标机器后，在 dist/ 目录下执行本脚本即可完成部署，
# 使 bin/qf_scmd 原地运行。所有系统路径通过软链接指向 dist/ 内对应文件。
#
# 用法:
#   sudo ./dist_depoly.sh          部署（建立软链接）
#   sudo ./dist_depoly.sh --remove 卸载（删除软链接及系统目录）
# ============================================================================

set -e

# 脚本所在目录即 dist/ 的绝对路径
DIST_DIR="$(cd "$(dirname "$0")" && pwd)"

# 颜色输出
info()  { echo -e "\033[1;34m[INFO]\033[0m  $*"; }
warn()  { echo -e "\033[1;33m[WARN]\033[0m  $*"; }
error() { echo -e "\033[1;31m[ERROR]\033[0m $*" >&2; }

# 检查 root 权限
check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        error "需要 root 权限，请使用 sudo 执行"
        exit 1
    fi
}

# 创建软链接（若目标已存在则跳过或覆盖）
make_link() {
    local src="$1"
    local dst="$2"

    if [ -L "${dst}" ]; then
        # 已存在软链接，更新
        ln -sf "${src}" "${dst}"
    elif [ -e "${dst}" ]; then
        warn "目标已存在且非软链接，跳过: ${dst}"
        return
    else
        ln -s "${src}" "${dst}"
    fi
}

# 部署
do_deploy() {
    info "部署目录: ${DIST_DIR}"

    # --- /etc/qifeng-scm/ 配置文件 ---
    info "部署配置文件 → /etc/qifeng-scm/"
    sudo mkdir -p /etc/qifeng-scm
    make_link "${DIST_DIR}/.config/scmd.yaml"     /etc/qifeng-scm/scmd.yaml
    make_link "${DIST_DIR}/.config/selftest.json"  /etc/qifeng-scm/selftest.json

    # --- /usr/lib/qifeng-scm/ 脚本 ---
    info "部署脚本 → /usr/lib/qifeng-scm/"
    sudo mkdir -p /usr/lib/qifeng-scm
    # 自检脚本
    if [ -f "${DIST_DIR}/scripts/self-check" ]; then
        make_link "${DIST_DIR}/scripts/self-check" /usr/lib/qifeng-scm/self-check
        sudo chmod +x /usr/lib/qifeng-scm/self-check
    fi
    if [ -f "${DIST_DIR}/scripts/config.ini" ]; then
        make_link "${DIST_DIR}/scripts/config.ini" /etc/qifeng-scm/self-check.ini
    fi

    # --- /usr/bin/ 可执行文件 ---
    info "部署可执行文件 → /usr/bin/"
    make_link "${DIST_DIR}/bin/qf_scmd" /usr/bin/qf_scmd
    make_link "${DIST_DIR}/bin/qf_scmc" /usr/bin/qf_scmc
    sudo chmod +x /usr/bin/qf_scmd /usr/bin/qf_scmc

    # --- /opt/sophon/selftest/ 模型文件 ---
    info "部署模型文件 → /opt/sophon/selftest/"
    sudo mkdir -p /opt/sophon/selftest
    for model in "${DIST_DIR}/model/"*; do
        if [ -e "${model}" ]; then
            make_link "${model}" "/opt/sophon/selftest/$(basename "${model}")"
        fi
    done

    # --- /etc/ld.so.conf.d/ 动态库搜索路径 ---
    info "配置动态库搜索路径"
    if [ -f "${DIST_DIR}/ld.so.conf.d/qifeng-scm.conf" ]; then
        sudo cp "${DIST_DIR}/ld.so.conf.d/qifeng-scm.conf" /etc/ld.so.conf.d/qifeng-scm.conf
    else
        echo "/usr/lib/qifeng-scm" | sudo tee /etc/ld.so.conf.d/qifeng-scm.conf > /dev/null
    fi
    sudo ldconfig

    # --- systemd 服务 ---
    info "部署 systemd 服务"
    if [ -f "${DIST_DIR}/systemd/qifeng-scmd.service" ]; then
        sudo cp "${DIST_DIR}/systemd/qifeng-scmd.service" /lib/systemd/system/qifeng-scmd.service
        sudo systemctl daemon-reload
        sudo systemctl enable qifeng-scmd.service
    fi

    # --- 创建运行时目录 ---
    sudo mkdir -p /var/lib/qifeng-scm/services
    sudo mkdir -p /var/lib/qifeng-scm/data
    sudo mkdir -p /var/lib/qifeng-scm/backup
    sudo mkdir -p /var/log/qifeng-scm
    sudo mkdir -p /run/qifeng-scm

    echo ""
    info "部署完成，可通过以下命令启动服务:"
    info "  sudo systemctl start qifeng-scmd"
    info "  sudo qf_scmc selftest"
}

# 卸载
do_remove() {
    info "卸载 qifeng-scm 部署..."

    # 停止服务
    sudo systemctl stop qifeng-scmd.service 2>/dev/null || true
    sudo systemctl disable qifeng-scmd.service 2>/dev/null || true

    # 删除软链接
    sudo rm -f /etc/qifeng-scm/scmd.yaml
    sudo rm -f /etc/qifeng-scm/selftest.json
    sudo rm -f /etc/qifeng-scm/self-check.ini
    sudo rm -f /usr/lib/qifeng-scm/self-check
    sudo rm -f /usr/bin/qf_scmd
    sudo rm -f /usr/bin/qf_scmc
    sudo rm -f /opt/sophon/selftest/fsmn_fp32_.bmodel
    sudo rm -f /lib/systemd/system/qifeng-scmd.service
    sudo rm -f /etc/ld.so.conf.d/qifeng-scm.conf
    sudo ldconfig

    # 清理空目录
    sudo rmdir /etc/qifeng-scm 2>/dev/null || true
    sudo rmdir /opt/sophon/selftest 2>/dev/null || true
    sudo rmdir /usr/lib/qifeng-scm 2>/dev/null || true

    sudo systemctl daemon-reload

    info "卸载完成"
}

# 主逻辑
check_root

case "${1:-}" in
    --remove|-r)
        do_remove
        ;;
    *)
        do_deploy
        ;;
esac
