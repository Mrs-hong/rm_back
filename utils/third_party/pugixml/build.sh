#!/bin/bash
# ==============================================================================
# build.sh - 拉取 pugixml 源码并编译为静态库
#
# pugixml 是轻量级 C++ XML 处理库
# 许可证：MIT License（允许免费闭源商用）
#
# 用法：
#   ./build.sh [OPTIONS]
#
# 产物目录结构：
#   utils/third_party/pugixml/
#   ├── include/
#   │   ├── pugixml.hpp
#   │   └── pugiconfig.hpp
#   └── lib/
#       └── libpugixml.a
#
# 依赖：C++17 编译器（g++ >= 7）、curl
# ==============================================================================

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
        rm -rf src
        rm -rf include
        rm -rf lib
    fi
    log_info "清理完成"
}

# 自动清理构建中间产物（保留 include 和 lib 目录）
auto_cleanup() {
    log_info "自动清理构建中间产物..."
    rm -rf src 2>/dev/null || log_warn "src 目录不存在，跳过清理"
    log_success "构建中间产物清理完成，仅保留 include 和 lib 目录"
}

show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "构建 pugixml 静态库的脚本"
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
    local tools=("g++" "curl" "ar")
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

download_sources() {
    local src_dir="src"
    local version_tag="master"

    # pugixml 源码仅 3 个文件，直接下载单文件
    # 优先 GitHub，失败回退 Gitee 镜像
    local files=(pugixml.cpp pugixml.hpp pugiconfig.hpp)
    local github_base="https://raw.githubusercontent.com/zeux/pugixml/${version_tag}/src"
    local gitee_base="https://gitee.com/mirrors/pugixml/raw/${version_tag}/src"

    if [[ -d "$src_dir" ]]; then
        local all_present=true
        for f in "${files[@]}"; do
            if [[ ! -f "${src_dir}/${f}" ]]; then
                all_present=false
                break
            fi
        done
        if [[ "$all_present" == true ]]; then
            log_warn "源码目录 '$src_dir' 已存在且完整，跳过下载"
            return
        fi
    fi

    rm -rf "$src_dir"
    mkdir -p "$src_dir"

    log_info "下载 pugixml 源码 (版本: ${version_tag})..."
    local download_success=false
    for base_url in "$github_base" "$gitee_base"; do
        log_info "尝试下载源: $base_url"
        local all_ok=true
        for f in "${files[@]}"; do
            if curl -sL --connect-timeout 10 "${base_url}/${f}" -o "${src_dir}/${f}" 2>/dev/null; then
                if [ -s "${src_dir}/${f}" ] && ! head -1 "${src_dir}/${f}" | grep -q "<!DOCTYPE\|<html\|404\|Not Found"; then
                    log_info "  OK: ${f}"
                else
                    log_warn "  失败: ${f} (无效内容)"
                    all_ok=false
                    break
                fi
            else
                log_warn "  失败: ${f}"
                all_ok=false
                break
            fi
        done
        if [ "$all_ok" = true ]; then
            download_success=true
            break
        fi
    done

    if [ "$download_success" = false ]; then
        log_error "无法从 GitHub 或 Gitee 下载 pugixml 源码"
        exit 1
    fi

    log_success "源码准备完成 (版本 ${version_tag})"
}

build_pugixml() {
    local src_dir="src"
    local include_dir="$(pwd)/include"
    local lib_dir="$(pwd)/lib"

    mkdir -p "$include_dir"
    mkdir -p "$lib_dir"

    log_info "编译 pugixml..."
    cd "$src_dir" || exit 1

    # -fPIC: 生成位置无关代码,既能链接到静态库也能链接到动态库
    # 注意:不要加 -fPIE,它会覆盖 -fPIC 生成 PIE 专用代码,
    #       其 R_X86_64_PC32 重定位无法用于共享库链接
    g++ -std=c++17 -O2 -Wall -fPIC -c -o pugixml.o pugixml.cpp || log_error "编译 pugixml.cpp 失败"

    log_info "打包 libpugixml.a..."
    ar rcs "${lib_dir}/libpugixml.a" pugixml.o || log_error "打包 libpugixml.a 失败"

    log_info "安装头文件..."
    cp pugixml.hpp "$include_dir/" || log_error "拷贝 pugixml.hpp 失败"
    cp pugiconfig.hpp "$include_dir/" || log_error "拷贝 pugiconfig.hpp 失败"

    cd .. || exit 1
    log_success "构建完成"
}

main() {
    log_info "开始构建 pugixml..."
    cleanup
    check_requirements
    download_sources
    build_pugixml

    # 构建成功后自动清理中间产物
    auto_cleanup

    log_success "pugixml 构建成功！"
    log_info "头文件: $(pwd)/include"
    log_info "静态库: $(pwd)/lib"
}

trap 'handle_error "$LINENO" "$BASH_COMMAND"' ERR

main
