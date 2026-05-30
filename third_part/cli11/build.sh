#!/bin/bash

cd "$(dirname "$0")" || exit 1

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
        rm -rf build
        rm -rf include
        rm -rf install
        rm -rf cli11_src
    fi
    log_info "清理完成"
}

# 自动清理构建中间产物（保留include和install目录）
auto_cleanup() {
    log_info "自动清理构建中间产物..."
    rm -rf build 2>/dev/null || log_warn "build目录不存在，跳过清理"
    rm -rf cli11_src 2>/dev/null || log_warn "源码目录不存在，跳过清理"
    log_success "构建中间产物清理完成，仅保留include和install目录"
}

show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "构建 CLI11 头文件库的脚本"
    echo ""
    echo "Options:"
    echo "  -h, --help      显示帮助信息"
    echo "  -v, --verbose   显示详细日志"
    echo "  -c, --clean     清理构建目录"
    echo ""
    echo "Examples:"
    echo "  $0"
    echo "  $0 --verbose"
    echo "  $0 --clean"
}

VERBOSE=false
CLEAN=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help) show_help; exit 0 ;;
        -v|--verbose) VERBOSE=true; set -x ;;
        -c|--clean) CLEAN=true ;;
        *) log_error "未知参数: $1"; show_help; exit 1 ;;
    esac
    shift
done

check_requirements() {
    log_info "检查必要的构建工具..."
    local tools=("git" "cmake" "make")
    local missing=()
    for t in "${tools[@]}"; do
        if ! command -v "$t" &> /dev/null; then
            missing+=($t)
        fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        log_error "缺少必要工具: ${missing[*]}"
        exit 1
    fi
    log_success "所有必要工具已安装"
}

clone_or_update_repo() {
    local repo_dir="cli11_src"
    local version_tag="v2.2.0"
    local repo_urls=(
        "https://gitcode.com/gh_mirrors/CLI11.git"
        "https://github.com/CLIUtils/CLI11.git"
    )

    if [[ -d "$repo_dir" ]]; then
        log_warn "源码目录 '$repo_dir' 已存在，跳过下载和更新"
        return
    else
        log_info "克隆 CLI11 仓库（指定版本，国内源优先）..."
        local cloned=false
        for repo_url in "${repo_urls[@]}"; do
            log_info "尝试下载源: $repo_url"
            if git clone --branch "$version_tag" --depth 1 "$repo_url" "$repo_dir"; then
                cloned=true
                break
            fi
            log_warn "下载源失败: $repo_url"
            rm -rf "$repo_dir"
        done

        if [[ "$cloned" != true ]]; then
            log_error "所有下载源均失败，请检查网络或手动下载到 '$repo_dir'"
            exit 1
        fi
    fi

    if [[ ! -d "$repo_dir" ]]; then
        log_error "源码目录不存在，请确保 '$repo_dir' 已存在且完整"
        exit 1
    fi

    log_success "源码准备完成 (版本 $version_tag)"
}

build_cli11() {
    local src_dir="cli11_src"
    local build_dir="build"
    local install_dir="$(pwd)/install"

    mkdir -p "$build_dir"
    mkdir -p "$install_dir"
    cd "$build_dir" || exit 1

    log_info "执行 cmake 配置..."
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
          -DCLI11_BUILD_TESTS=OFF \
          -DCLI11_BUILD_EXAMPLES=OFF \
          -DCLI11_BUILD_DOCS=OFF \
          -DCMAKE_INSTALL_PREFIX=$install_dir \
          "../$src_dir" || exit 1

    log_info "执行 make install..."
    make install || exit 1
    log_success "构建完成"

    log_info "拷贝头文件..."
    mkdir -p ../include
    cp -r "$install_dir/include/CLI/" ../include/ || exit 1

    log_success "文件拷贝完成"
    cd .. || exit 1
}

main() {
    log_info "开始构建 CLI11..."
    cleanup
    check_requirements
    clone_or_update_repo
    build_cli11

    # 构建成功后自动清理中间产物
    auto_cleanup

    log_success "CLI11 构建成功！"
    log_info "头文件: $(pwd)/include"
}

trap 'handle_error "$LINENO" "$BASH_COMMAND"' ERR

main
