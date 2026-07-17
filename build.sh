#!/bin/bash

# ============================================================================
# qifeng-scm 编译脚本
# 用法:
#   ./build              - 编译主项目 (Release)
#   ./build -d           - 编译主项目 (Debug)
#   ./build -r           - 编译主项目 (Release)
#   ./build all -d       - 编译所有目标 (Debug)
#   ./build all -r       - 编译所有目标 (Release)
#   ./build test -d      - 编译测试目标 (Debug)
#   ./build test -r      - 编译测试目标 (Release)
#   ./build clean        - 清除 build 目录
#   ./build -r -i /opt/qifeng  - 编译并安装到指定目录
#   ./build download-deps - 下载预编译依赖
#
# checker 可选库参数（默认全部 ON，WSL2 交叉编译时可关闭）:
#   --with-all=ON|OFF      全部开启或关闭（快捷方式，可被单项覆盖）
#   --with-bm-sdk=ON|OFF   Sophon SDK (TPU/模型推理)
#   --with-alsa=ON|OFF     ALSA (麦克风)
#   --with-drm=ON|OFF      libdrm (显示器)
#   --with-gpiod=ON|OFF    libgpiod (GPIO 指示灯)
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
JOBS=$(nproc 2>/dev/null || echo 4)

# 颜色常量（与 script/download_dependency.sh 保持一致）
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; NC='\033[0m'

log_info()    { echo -e "${BLUE}[INFO]${NC}    $*"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $*"; }
log_warn()    { echo -e "${YELLOW}[WARN]${NC}    $*"; }
log_error()   { echo -e "${RED}[ERROR]${NC}   $*"; exit 1; }

# 解析全局参数
INSTALL_PREFIX=""
PARSED_ARGS=()
WITH_BM_SDK=""
WITH_ALSA=""
WITH_DRM=""
WITH_GPIOD=""
VERBOSE=false
FORCE=false

i=1
while [ $i -le $# ]; do
    arg="${!i}"
    case "$arg" in
        -i)
            i=$((i + 1))
            if [ $i -le $# ]; then
                INSTALL_PREFIX="${!i}"
            else
                log_error "-i 参数需要指定安装目录"
            fi
            ;;
        -v|--verbose)
            VERBOSE=true
            set -x
            ;;
        -f|--force)
            FORCE=true
            ;;
        --with-all=*)
            all_val="${arg#--with-all=}"
            WITH_BM_SDK="${all_val}"
            WITH_ALSA="${all_val}"
            WITH_DRM="${all_val}"
            WITH_GPIOD="${all_val}"
            ;;
        --with-bm-sdk=*)
            WITH_BM_SDK="${arg#--with-bm-sdk=}"
            ;;
        --with-alsa=*)
            WITH_ALSA="${arg#--with-alsa=}"
            ;;
        --with-drm=*)
            WITH_DRM="${arg#--with-drm=}"
            ;;
        --with-gpiod=*)
            WITH_GPIOD="${arg#--with-gpiod=}"
            ;;
        *)
            PARSED_ARGS+=("$arg")
            ;;
    esac
    i=$((i + 1))
done

# 解析构建类型: -d=Debug, -r=Release
parse_build_type() {
    case "$1" in
        -d) echo "Debug" ;;
        -r) echo "Release" ;;
        *)  echo "Release" ;;
    esac
}

# 检测 framework 是否就绪，缺失则自动下载
ensure_framework() {
    local fw_dir="${SCRIPT_DIR}/third_part/qifeng_framework/install"
    local fw_config="${fw_dir}/lib/cmake/qifeng_framework/qifeng_framework-config.cmake"
    if [ ! -f "$fw_config" ]; then
        log_info "qifeng_framework 未安装，尝试自动下载..."
        if [ -f "${SCRIPT_DIR}/script/build_script/download_dependency.sh" ]; then
            local version="${QIFENG_FRAMEWORK_VERSION:-2.0.2}"
            local arch=""
            case "$(uname -m)" in
                x86_64|amd64) arch="x86" ;;
                aarch64|arm64) arch="arm" ;;
                *) arch="x86" ;;
            esac
            if ! bash "${SCRIPT_DIR}/script/build_script/download_dependency.sh" \
                --component qifeng_framework \
                --version "$version" \
                --arch "$arch"; then
                log_error "qifeng_framework 依赖下载失败"
                exit 1
            fi
        else
            log_error "依赖下载脚本不存在: script/build_script/download_dependency.sh"
            log_info "请先执行: GITLAB_TOKEN=xxx ./build download-deps"
            exit 1
        fi
    fi
}

# 执行 cmake 配置
run_cmake() {
    local build_type="$1"
    local testing="$2"

    ensure_framework
    mkdir -p "${BUILD_DIR}"
    local cmake_args=(
        -DCMAKE_BUILD_TYPE="${build_type}"
        -DBUILD_TESTING="${testing}"
    )

    # checker 可选库参数（仅显式指定时才传入，否则使用 CMake 默认值）
    [ -n "${WITH_BM_SDK}" ]  && cmake_args+=(-DWITH_BM1684_SDK="${WITH_BM_SDK}")
    [ -n "${WITH_ALSA}" ]    && cmake_args+=(-DWITH_ALSA="${WITH_ALSA}")
    [ -n "${WITH_DRM}" ]     && cmake_args+=(-DWITH_DRM="${WITH_DRM}")
    [ -n "${WITH_GPIOD}" ]   && cmake_args+=(-DWITH_GPIOD="${WITH_GPIOD}")

    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" "${cmake_args[@]}"
}

# 执行 make 编译
run_make() {
    local target="$1"
    if [ -z "${target}" ]; then
        cmake --build "${BUILD_DIR}" -j${JOBS}
    else
        cmake --build "${BUILD_DIR}" --target "${target}" -j${JOBS}
    fi
}

# 执行安装
run_install() {
    if [ -n "${INSTALL_PREFIX}" ]; then
        echo "=== 安装到 ${INSTALL_PREFIX} ==="
        cmake --install "${BUILD_DIR}" --prefix "${INSTALL_PREFIX}"
    fi
}


# 主逻辑
set -- "${PARSED_ARGS[@]}"

case "$1" in
    clean|-clean)
        if [ -d "${BUILD_DIR}" ]; then
            rm -rf "${BUILD_DIR}"
            log_success "已完全清除 build 目录"
        else
            log_info "build 目录不存在，无需清除"
        fi
        exit 0
        ;;
    all)
        BUILD_TYPE=$(parse_build_type "$2")
        log_info "编译所有目标 | ${BUILD_TYPE} | -j${JOBS}"
        run_cmake "${BUILD_TYPE}" ON
        run_make
        run_install
        ;;
    test)
        BUILD_TYPE=$(parse_build_type "$2")
        log_info "编译测试目标 | ${BUILD_TYPE} | -j${JOBS}"
        run_cmake "${BUILD_TYPE}" ON
        run_make test_config
        run_install
        ;;
    -d|-r|"")
        BUILD_TYPE=$(parse_build_type "$1")
        log_info "编译主项目 | ${BUILD_TYPE} | -j${JOBS}"
        run_cmake "${BUILD_TYPE}" OFF
        run_make
        run_install
        ;;
    download-deps)
        if [ -f "${SCRIPT_DIR}/script/build_script/download_dependency.sh" ]; then
            log_info "开始下载预编译依赖..."
            local version="${QIFENG_FRAMEWORK_VERSION:-2.0.2}"
            local arch=""
            case "$(uname -m)" in
                x86_64|amd64) arch="x86" ;;
                aarch64|arm64) arch="arm" ;;
                *) arch="x86" ;;
            esac
            if ! bash "${SCRIPT_DIR}/script/build_script/download_dependency.sh" \
                --component qifeng_framework \
                --version "$version" \
                --arch "$arch" ${FORCE:+--force}; then
                log_error "依赖下载失败"
                exit 1
            fi
        else
            log_error "依赖下载脚本不存在: script/build_script/download_dependency.sh"
        fi
        ;;
    *)
        echo "用法: $0 [all|test|clean|download-deps] [-d|-r] [-i <install_prefix>] [-v|--verbose] [-f|--force] [--with-*=ON|OFF]"
        echo ""
        echo "构建命令:"
        echo "  $0                          编译主项目 (Release)"
        echo "  $0 -d                       编译主项目 (Debug)"
        echo "  $0 -r                       编译主项目 (Release)"
        echo "  $0 all -d                   编译所有目标 (Debug)"
        echo "  $0 all -r                   编译所有目标 (Release)"
        echo "  $0 test -d                  编译测试目标 (Debug)"
        echo "  $0 test -r                  编译测试目标 (Release)"
        echo "  $0 -r -i /opt/qifeng        编译并安装到指定目录"
        echo "  $0 clean                    清除 build 目录"
        echo "  $0 download-deps            下载预编译依赖(需设置 GITLAB_TOKEN)"
        echo ""
        echo "通用选项:"
        echo "  -v, --verbose               显示详细日志"
        echo "  -f, --force                 强制重新下载依赖"
        echo ""
        echo "checker 可选库参数（默认全部 ON，WSL2 交叉编译时可关闭）:"
        echo "  --with-all=ON|OFF           全部开启或关闭（快捷方式，可被单项覆盖）"
        echo "  --with-bm-sdk=ON|OFF        Sophon SDK (TPU/模型推理, 默认ON)"
        echo "  --with-alsa=ON|OFF          ALSA (麦克风, 默认ON)"
        echo "  --with-drm=ON|OFF           libdrm (显示器, 默认ON)"
        echo "  --with-gpiod=ON|OFF         libgpiod (GPIO指示灯, 默认ON)"
        echo ""
        echo "示例:"
        echo "  $0 -r --with-all=OFF                    WSL2交叉编译(关闭全部可选库)"
        echo "  $0 -r --with-all=OFF --with-gpiod=ON    关闭全部但仅启用GPIO"
        echo "  $0 download-deps -f                     强制重新下载依赖"
        echo "  GITLAB_TOKEN=glpat-xxx $0 download-deps 指定 Token 下载依赖"
        exit 1
        ;;
esac
