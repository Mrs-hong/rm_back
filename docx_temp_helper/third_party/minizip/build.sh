#!/bin/bash
# ==============================================================================
# build.sh - 拉取 minizip 源码并编译为静态库
#
# minizip 是 zlib contrib 中的 ZIP 读写库
# 许可证：zlib License（允许免费闭源商用）
#
# 用法：
#   ./build.sh [OPTIONS]
#
# 产物目录结构：
#   third_party/minizip/
#   ├── include/
#   │   ├── zip.h
#   │   ├── unzip.h
#   │   └── ioapi.h
#   └── lib/
#       └── libminizip.a
#
# 依赖：zlib（系统安装 zlib1g-dev）、gcc、curl
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
    echo "构建 minizip 静态库的脚本"
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
    local tools=("gcc" "curl" "ar")
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

    # 检查 zlib 依赖（优先 pkg-config，回退到检查头文件）
    if command -v pkg-config &> /dev/null && pkg-config --exists zlib 2>/dev/null; then
        log_success "zlib 依赖已确认 (pkg-config)"
    elif [ -f /usr/include/zlib.h ]; then
        log_warn "pkg-config 不可用或未找到 zlib，但检测到 /usr/include/zlib.h"
    else
        log_error "缺少 zlib 依赖，请安装: sudo apt install zlib1g-dev"
        exit 1
    fi

    log_success "所有必要工具已安装"
}

download_sources() {
    local src_dir="src"
    local version_tag="v1.3.1"

    # 使用稳定版 v1.3.1（master 分支有额外依赖如 skipset.h）
    # 优先 GitHub，失败回退 Gitee 镜像
    local files=(zip.c zip.h unzip.c unzip.h ioapi.c ioapi.h crypt.h)
    local github_base="https://raw.githubusercontent.com/madler/zlib/${version_tag}/contrib/minizip"
    local gitee_base="https://gitee.com/mirrors/zlib/raw/${version_tag}/contrib/minizip"

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

    log_info "下载 minizip 源码 (版本: ${version_tag})..."
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
        log_error "无法从 GitHub 或 Gitee 下载 minizip 源码"
        exit 1
    fi

    log_success "源码准备完成 (版本 ${version_tag})"
}

build_minizip() {
    local src_dir="src"
    local include_dir="$(pwd)/include"
    local lib_dir="$(pwd)/lib"

    mkdir -p "$include_dir"
    mkdir -p "$lib_dir"

    # 获取 zlib 头文件路径
    local zlib_include
    zlib_include=$(pkg-config --cflags-only-I zlib 2>/dev/null || echo "")
    if [ -z "$zlib_include" ]; then
        zlib_include="-I/usr/include"
    fi

    log_info "编译 minizip..."
    cd "$src_dir" || exit 1

    # 编译每个 .c 文件为 .o
    for f in zip.c unzip.c ioapi.c; do
        log_info "  编译 ${f}..."
        gcc -c -O2 -Wall -fPIE $zlib_include -o "${f%.c}.o" "$f" || log_error "编译 ${f} 失败"
    done

    log_info "打包 libminizip.a..."
    ar rcs "${lib_dir}/libminizip.a" zip.o unzip.o ioapi.o || log_error "打包 libminizip.a 失败"

    log_info "安装头文件..."
    cp zip.h "$include_dir/" || log_error "拷贝 zip.h 失败"
    cp unzip.h "$include_dir/" || log_error "拷贝 unzip.h 失败"
    cp ioapi.h "$include_dir/" || log_error "拷贝 ioapi.h 失败"

    cd .. || exit 1
    log_success "构建完成"
}

main() {
    log_info "开始构建 minizip..."
    cleanup
    check_requirements
    download_sources
    build_minizip

    # 构建成功后自动清理中间产物
    auto_cleanup

    log_success "minizip 构建成功！"
    log_info "头文件: $(pwd)/include"
    log_info "静态库: $(pwd)/lib"
}

trap 'handle_error "$LINENO" "$BASH_COMMAND"' ERR

main
