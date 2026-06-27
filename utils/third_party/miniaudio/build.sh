#!/bin/bash
# ==============================================================================
# build.sh - 拉取 miniaudio 单头文件
#
# miniaudio 是 C 语言音频输入/输出库，header-only 无需编译
# 许可证：Public Domain 或 MIT-0（允许免费闭源商用）
#
# 用法：
#   ./build.sh [OPTIONS]
#
# 产物目录结构：
#   utils/third_party/miniaudio/
#   └── miniaudio.h
#
# 依赖：curl
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
    exit $exit_code
}

show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "下载 miniaudio 单头文件 (header-only,无需编译)"
    echo ""
    echo "Options:"
    echo "  -h, --help      显示帮助信息"
    echo "  -v, --verbose   显示详细日志"
    echo "  -f, --force     强制重新下载，覆盖已有头文件"
    echo ""
    echo "Examples:"
    echo "  $0"
    echo "  $0 --verbose"
    echo "  $0 --force"
}

VERBOSE=false
FORCE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help) show_help; exit 0 ;;
        -v|--verbose) VERBOSE=true; set -x ;;
        -f|--force) FORCE=true ;;
        *) log_error "未知参数: $1"; show_help; exit 1 ;;
    esac
    shift
done

check_requirements() {
    log_info "检查必要的构建工具..."
    if ! command -v curl &> /dev/null; then
        log_error "缺少必要工具: curl"
        exit 1
    fi
    log_success "所有必要工具已安装"
}

download_header() {
    local header_file="miniaudio.h"
    local version_tag="v0.11.25"

    # 优先 GitHub，失败回退 Gitee 镜像
    local github_url="https://raw.githubusercontent.com/mackron/miniaudio/${version_tag}/miniaudio.h"
    local gitee_url="https://gitee.com/mirrors/miniaudio/raw/${version_tag}/miniaudio.h"

    # 如果头文件已存在且未强制覆盖，跳过下载
    if [[ -f "$header_file" ]] && [[ "$FORCE" != true ]]; then
        log_warn "miniaudio.h 已存在，跳过下载（使用 -f 强制覆盖）"
        local version
        version=$(grep -m1 "miniaudio - " "$header_file" 2>/dev/null | sed 's/.*miniaudio - //' | sed 's/ -.*//')
        if [[ -n "$version" ]]; then
            log_info "当前版本: $version"
        fi
        return
    fi

    log_info "下载 miniaudio.h (分支: ${version_tag})..."
    local download_success=false

    for base_url in "$github_url" "$gitee_url"; do
        log_info "尝试下载源: $base_url"
        if curl -sL --connect-timeout 15 "$base_url" -o "${header_file}.tmp" 2>/dev/null; then
            if [ -s "${header_file}.tmp" ] && ! head -1 "${header_file}.tmp" | grep -q "<!DOCTYPE\|<html\|404\|Not Found"; then
                mv "${header_file}.tmp" "$header_file"
                download_success=true
                break
            else
                log_warn "  下载内容无效"
                rm -f "${header_file}.tmp"
            fi
        else
            log_warn "  下载失败"
        fi
    done

    if [ "$download_success" = false ]; then
        log_error "无法从 GitHub 或 Gitee 下载 miniaudio.h"
        exit 1
    fi

    local version
    version=$(grep -m1 "miniaudio - " "$header_file" 2>/dev/null | sed 's/.*miniaudio - //' | sed 's/ -.*//')
    log_success "miniaudio.h 下载完成 (版本: ${version:-unknown})"
}

main() {
    log_info "开始准备 miniaudio (header-only)..."
    check_requirements
    download_header

    log_success "miniaudio 准备完成！"
    log_info "头文件: $(pwd)/miniaudio.h"
    log_info "miniaudio 是 header-only 库，无需编译，直接在代码中 #include 即可使用"
}

trap 'handle_error "$LINENO" "$BASH_COMMAND"' ERR

main