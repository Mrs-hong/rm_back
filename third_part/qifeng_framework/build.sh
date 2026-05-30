#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

# 颜色常量
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_warn()    { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }


handle_error() {
    local exit_code=$?
    local line_number=$1
    local command="$2"
    log_error "命令执行失败，行号: $line_number, 命令: $command, 退出码: $exit_code"
    cleanup
    exit $exit_code
}

cleanup() {
    log_info "执行清理操作..."
    if [[ "$CLEAN" == true ]]; then
        log_info "清理构建目录..."
        rm -rf install
        rm qifeng_framework.tar.gz
    fi
    log_info "清理完成"
}

# 自动清理构建中间产物（保留include和lib目录）
auto_cleanup() {
    log_info "自动清理中间产物..."
    # 清理源代码目录
    rm -rf qifeng_framework.tar.gz 2>/dev/null || log_warn "包不存在，跳过清理"
    log_success "中间产物清理完成"
}

get_tar() {
    local name_tar="qifeng_framework.tar.gz"
    local install="install"
    local tar_url='http://10.244.219.144:9700/generic/6hKc7W5ai9bd/qifeng_framework_backup_20260429_165104.tar.gz?version=V100R026C00B0429'

    if [[ -d "$install" ]]; then
        log_warn "$install 已存在，跳过下载"
        return
    else 
        curl -fL -u "pull_user:qfss2025+" ${tar_url} -o $name_tar
        tar -zxvf $name_tar
    fi
}

main() {
    log_info "开始拉取 qifeng_framework 包..."
    cleanup

    get_tar

    auto_cleanup
    
    log_success "拉取成功！"
}

trap 'handle_error "$LINENO" "$BASH_COMMAND"' ERR

main

