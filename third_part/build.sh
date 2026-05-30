#!/bin/bash

# 确保工作目录始终是脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

# 定义颜色常量
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 定义日志函数
log_info()    { echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') $1"; }
log_warn()    { echo -e "${YELLOW}[WARN]${NC} $(date '+%Y-%m-%d %H:%M:%S') $1"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') $1"; }
log_header()  { echo -e "${CYAN}========================================${NC}"; }
log_section() { echo -e "${CYAN}--- $1 ${NC}"; }

# 全局变量
BUILD_LOG_FILE="${SCRIPT_DIR}/build_all_$(date '+%Y%m%d_%H%M%S').log"
TOTAL_COMPONENTS=0
SUCCESS_COMPONENTS=0
FAILED_COMPONENTS=0
SKIP_COMPONENTS=0
VERBOSE=false
CLEAN=false
FORCE=false
PARALLEL=false
BUILD_ORDER_FILE="build_order.txt"

# 错误处理
handle_error() {
    local exit_code=$?
    local line_number=$1
    local command="$2"
    log_error "命令执行失败，行号：$line_number, 命令：$command, 退出码：$exit_code"
    exit $exit_code
}

trap 'handle_error $LINENO "$BASH_COMMAND"' ERR

# 显示帮助信息
show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "全局第三方组件自动构建脚本"
    echo ""
    echo "Options:"
    echo "  -h, --help              显示帮助信息"
    echo "  -v, --verbose           显示详细构建日志"
    echo "  -c, --clean             清理所有组件的构建产物"
    echo "  -f, --force             强制重新构建所有组件"
    echo "  -p, --parallel          并行构建（实验性）"
    echo "  -o, --order FILE        指定构建顺序文件"
    echo "  -l, --log FILE          指定日志文件路径"
    echo ""
    echo "Examples:"
    echo "  $0                      # 执行默认构建"
    echo "  $0 --verbose            # 执行详细构建"
    echo "  $0 --clean              # 清理所有组件"
    echo "  $0 --parallel           # 并行构建"
    echo "  $0 --order custom.txt   # 使用自定义构建顺序"
    echo ""
    echo "构建规则:"
    echo "  1. 自动扫描 third_part 目录下所有包含 build.sh 的子目录"
    echo "  2. 按照目录字母顺序执行构建（除非指定构建顺序文件）"
    echo "  3. 自动检测依赖关系并处理"
    echo "  4. 构建失败时继续处理其他组件"
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help) show_help; exit 0 ;;
        -v|--verbose) VERBOSE=true; set -x ;;
        -c|--clean) CLEAN=true ;;
        -f|--force) FORCE=true ;;
        -p|--parallel) PARALLEL=true ;;
        -o|--order) BUILD_ORDER_FILE="$2"; shift ;;
        -l|--log) BUILD_LOG_FILE="$2"; shift ;;
        -a|--all) ALL_COMPONENTS=true ;;
        *) log_error "未知参数：$1"; show_help; exit 1 ;;
    esac
    shift
done

# 设置日志文件
exec > >(tee -a "$BUILD_LOG_FILE") 2>&1

log_header
log_info "全局第三方组件构建脚本启动"
log_info "工作目录：$SCRIPT_DIR"
log_info "日志文件：$BUILD_LOG_FILE"
log_header

# 检查必要工具
check_requirements() {
    log_info "检查必要工具..."
    local tools=("git" "cmake" "make" "gcc" "g++")
    local missing=()
    
    for tool in "${tools[@]}"; do
        if ! command -v "$tool" &> /dev/null; then
            missing+=("$tool")
        fi
    done
    
    if [[ ${#missing[@]} -gt 0 ]]; then
        log_warn "缺少工具：${missing[*]}"
        log_info "部分组件可能无法构建"
    else
        log_success "所有必要工具已安装"
    fi
}


# 扫描第三方组件目录
scan_components() {
    log_section "扫描第三方组件"
    local components=()
    
    # 遍历 third_part 目录下的所有子目录
    for dir in "$SCRIPT_DIR"/*/; do
        if [[ -d "$dir" ]]; then
            local component_name=$(basename "$dir")
            local build_script="${dir}build.sh"
            
            if [[ -f "$build_script" ]]; then
                components+=("$component_name")
                log_info "发现组件：$component_name"
            else
                log_warn "组件 $component_name 缺少 build.sh 脚本，跳过"
            fi
        fi
    done
    
    # 按字母顺序排序
    IFS=$'\n' SORTED_COMPONENTS=($(sort <<<"${components[*]}"))
    unset IFS
    
    TOTAL_COMPONENTS=${#SORTED_COMPONENTS[@]}
    
    if [[ $TOTAL_COMPONENTS -eq 0 ]]; then
        log_error "未找到任何可构建的组件"
        exit 1
    fi
    
    log_success "共发现 $TOTAL_COMPONENTS 个可构建组件"
}

# 读取自定义构建顺序
read_build_order() {
    if [[ -f "$SCRIPT_DIR/$BUILD_ORDER_FILE" ]]; then
        log_info "读取构建顺序文件：$BUILD_ORDER_FILE"
        mapfile -t SORTED_COMPONENTS < "$SCRIPT_DIR/$BUILD_ORDER_FILE"
        TOTAL_COMPONENTS=${#SORTED_COMPONENTS[@]}
    fi
}

# 检测组件依赖关系
detect_dependencies() {
    local component="$1"
    local build_script="${SCRIPT_DIR}/${component}/build.sh"
    local dependencies=()
    
    # 检查 build.sh 中是否定义了依赖
    if grep -q "DEPENDENCIES=" "$build_script" 2>/dev/null; then
        # 提取依赖声明
        local deps_line=$(grep "DEPENDENCIES=" "$build_script")
        dependencies=($(echo "$deps_line" | sed 's/.*DEPENDENCIES=\[//;s/\].*//'))
    fi
    
    echo "${dependencies[@]}"
}

# 执行单个组件构建
build_component() {
    local component="$1"
    local build_script="${SCRIPT_DIR}/${component}/build.sh"
    local start_time=$(date +%s)
    
    log_section "构建组件：$component"
    log_info "构建脚本：$build_script"
    
    # 检查构建脚本是否存在
    if [[ ! -f "$build_script" ]]; then
        log_error "构建脚本不存在：$build_script"
        ((FAILED_COMPONENTS++))
        return 1
    fi
    
    # 检查构建脚本是否可执行
    if [[ ! -x "$build_script" ]]; then
        log_info "添加执行权限..."
        chmod +x "$build_script"
    fi
    
    # 检查构建脚本支持的参数
    local supports_force=false
    local supports_verbose=false
    local supports_clean=false
    
    if grep -q "--force" "$build_script" 2>/dev/null; then
        supports_force=true
    fi
    if grep -q "--verbose" "$build_script" 2>/dev/null; then
        supports_verbose=true
    fi
    if grep -q "--clean" "$build_script" 2>/dev/null; then
        supports_clean=true
    fi
    
    # 构建参数
    local build_args=""
    [[ "$VERBOSE" == true && "$supports_verbose" == true ]] && build_args+=" --verbose"
    [[ "$CLEAN" == true && "$supports_clean" == true ]] && build_args+=" --clean"
    [[ "$FORCE" == true && "$supports_force" == true ]] && build_args+=" --force"
    
    # 执行构建
    log_info "执行构建命令：$build_script $build_args"
    
    if "$build_script" $build_args; then
        local end_time=$(date +%s)
        local duration=$((end_time - start_time))
        log_success "组件 $component 构建成功 (耗时：${duration}s)"
        ((SUCCESS_COMPONENTS++))
        return 0
    else
        local end_time=$(date +%s)
        local duration=$((end_time - start_time))
        log_error "组件 $component 构建失败 (耗时：${duration}s)"
        ((FAILED_COMPONENTS++))
        return 1
    fi
}

# 并行构建组件（实验性）
build_parallel() {
    log_info "启动并行构建模式..."
    local pids=()
    
    for component in "${SORTED_COMPONENTS[@]}"; do
        build_component "$component" &
        pids+=("$!")
        log_info "启动组件 $component 的构建进程 (PID: ${pids[-1]})"
    done
    
    # 等待所有进程完成
    for pid in "${pids[@]}"; do
        wait "$pid"
    done
}

# 顺序构建组件
build_sequential() {
    log_info "启动顺序构建模式..."
    
    for component in "${SORTED_COMPONENTS[@]}"; do
        # 检查依赖
        local deps=$(detect_dependencies "$component")
        if [[ -n "$deps" ]]; then
            log_info "组件 $component 依赖：$deps"
            # TODO: 实现依赖检查逻辑
        fi
        
        build_component "$component"
        
        # 如果强制模式，继续构建其他组件
        if [[ "$FORCE" != true ]]; then
            # 检查是否有构建失败
            if [[ $FAILED_COMPONENTS -gt 0 ]]; then
                # 检查是否是交互式终端
                if [[ -t 0 ]]; then
                    log_warn "遇到构建错误，是否继续？(y/n)"
                    read -r response
if [[ "$response" != "y" ]]; then
                        log_warn "构建已中断"
                        break
                    fi
                else
                    log_warn "非交互式终端，继续构建其他组件"
                fi
            fi
        fi
    done
}

# 生成构建报告
generate_report() {
    log_header
    log_section "构建报告"
    
    local end_time=$(date +%s)
    local total_duration=$((end_time - START_TIME))
    
    log_info "总组件数：$TOTAL_COMPONENTS"
    log_info "构建成功：$SUCCESS_COMPONENTS"
    log_info "构建失败：$FAILED_COMPONENTS"
    log_info "跳过组件：$SKIP_COMPONENTS"
    log_info "总耗时：${total_duration}s"
    log_info "日志文件：$BUILD_LOG_FILE"
    
    if [[ $FAILED_COMPONENTS -gt 0 ]]; then
        log_error "部分组件构建失败，请查看日志文件获取详细信息"
        return 1
    else
        log_success "所有组件构建成功！"
        return 0
    fi
}

# lib 拷贝
copy_lib() {
    cp */lib/* ../lib
}

# 主函数
main() {
    START_TIME=$(date +%s)
    
    # 检查必要工具
    check_requirements
    
    # 扫描组件
    scan_components
    
    # 读取自定义构建顺序（如果存在）
    read_build_order
    
    # 执行构建
    if [[ "$PARALLEL" == true ]]; then
        build_parallel
    else
        build_sequential
    fi

    # lib拷贝
    #copy_lib

    # 生成报告
    generate_report
    local exit_code=$?
    
    log_header
    log_info "构建脚本执行完成"
    log_header
    
    exit $exit_code
}

# 执行主函数
main
