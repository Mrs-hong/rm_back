#!/bin/bash
################################################################################
# 统一预编译依赖下载器 — 适配 qifeng-scm 项目
#
# 从 GitLab 通用包仓库下载指定组件的预编译 tar 包至 download/，
# 校验 SHA256 完整性后解压至 third_part/<component>/ 目录。
# 采用版本文件 (.version) 做增量下载缓存，避免重复下载。
#
# 用法:
#   ./script/download_dependency.sh --component qifeng_framework
#   ./script/download_dependency.sh --help
#
# 环境变量覆盖(优先级高于组件内置默认值):
#   GITLAB_URL         GitLab 基础地址
#   GITLAB_TOKEN       GitLab 访问令牌 (需 read_api 权限)
#   QIFENG_FRAMEWORK_VERSION  qifeng_framework 版本号
#   QIFENG_FRAMEWORK_PROJECT_ID  qifeng_framework 项目 ID
#   QIFENG_FRAMEWORK_PACKAGE_NAME  qifeng_framework 通用包名
################################################################################

set -euo pipefail

# ============================================================================
# 颜色与日志函数
# ============================================================================
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; NC='\033[0m'

log_info()    { echo -e "${BLUE}[INFO]${NC}  $(date '+%H:%M:%S') $*"; }
log_success() { echo -e "${GREEN}[OK]${NC}    $(date '+%H:%M:%S') $*"; }
log_warn()    { echo -e "${YELLOW}[WARN]${NC}  $(date '+%H:%M:%S') $*"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $(date '+%H:%M:%S') $*" >&2; }
log_header()  { echo -e "${CYAN}══════════════════════════════════════════════${NC}"; }

# ============================================================================
# 全局默认值
# ============================================================================
GITLAB_URL="${GITLAB_URL:-https://gitlab.internal.qifeng.ai}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 脚本在 script/build_script/ 下，需向上两级到项目根目录
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DEPS_DIR="${PROJECT_ROOT}/third_part"
DOWNLOAD_DIR="${PROJECT_ROOT}/download"

# ============================================================================
# 组件注册表
# ============================================================================
get_component_config() {
    local comp="$1"
    local key="$2"

    case "${comp}:${key}" in
        qifeng_framework:project_id)
            echo "${QIFENG_FRAMEWORK_PROJECT_ID:-41}" ;;
        qifeng_framework:package_name)
            echo "${QIFENG_FRAMEWORK_PACKAGE_NAME:-qifeng_framework}" ;;
        qifeng_framework:version)
            echo "${QIFENG_FRAMEWORK_VERSION:-2.0.2}" ;;
        qifeng_framework:file_prefix)
            echo "qifeng_framework" ;;
        qifeng_framework:strip_components)
            # Framework tar 无顶层 wrapper，直接解压
            echo "0" ;;
        qifeng_framework:install_subdir)
            # 安装到 third_part/qifeng_framework/install/
            echo "install" ;;

        *)
            echo "" ;;
    esac
}

is_valid_component() {
    local comp="$1"
    [[ -n "$(get_component_config "$comp" "project_id")" ]]
}

# ============================================================================
# 架构检测
# ============================================================================
detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64) echo "x86" ;;
        aarch64|arm64) echo "arm" ;;
        *) log_error "不支持的架构: $(uname -m) (仅支持 x86_64 / aarch64)"; return 1 ;;
    esac
}

# ============================================================================
# 参数解析
# ============================================================================
COMPONENT=""
TARGET_ARCH=""
CLI_VERSION=""
FORCE=false

show_help() {
    sed -n '3,24p' "$0"
    echo ""
    echo "已注册组件:"
    for comp in qifeng_framework; do
        local ver pid pkg
        ver="$(get_component_config "$comp" "version")"
        pid="$(get_component_config "$comp" "project_id")"
        pkg="$(get_component_config "$comp" "package_name")"
        printf "  %-22s  project=%-4s  package=%-22s  version=%s\n" \
            "$comp" "$pid" "$pkg" "$ver"
    done
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)   show_help; exit 0 ;;
        --component) COMPONENT="$2"; shift 2 ;;
        --arch)      TARGET_ARCH="$2"; shift 2 ;;
        --version)   CLI_VERSION="$2"; shift 2 ;;
        --force)     FORCE=true; shift ;;
        *) log_error "未知参数: $1"; show_help; exit 1 ;;
    esac
done

if [[ -z "$COMPONENT" ]]; then
    log_error "缺少必要参数: --component"
    show_help
    exit 1
fi

if ! is_valid_component "$COMPONENT"; then
    log_error "未注册的组件: ${COMPONENT}"
    log_info "可用组件: qifeng_framework"
    exit 1
fi

[[ -z "$TARGET_ARCH" ]] && TARGET_ARCH="$(detect_arch)"

# 读取组件配置
PROJECT_ID="$(get_component_config "$COMPONENT" "project_id")"
PACKAGE_NAME="$(get_component_config "$COMPONENT" "package_name")"
PACKAGE_VERSION="$(get_component_config "$COMPONENT" "version")"
# --version 命令行参数优先级最高，覆盖配置默认值
[[ -n "$CLI_VERSION" ]] && PACKAGE_VERSION="$CLI_VERSION"
FILE_PREFIX="$(get_component_config "$COMPONENT" "file_prefix")"
STRIP_COMPONENTS="$(get_component_config "$COMPONENT" "strip_components")"
INSTALL_SUBDIR="$(get_component_config "$COMPONENT" "install_subdir")"

# 安装目标目录: third_part/<component>/install/
INSTALL_DIR="${DEPS_DIR}/${COMPONENT}/${INSTALL_SUBDIR}"
VERSION_FILE="${INSTALL_DIR}/.version"

# 远程文件名
TAR_FILE="${FILE_PREFIX}_${TARGET_ARCH}_${PACKAGE_VERSION}.tar.gz"
SHA_FILE="${TAR_FILE}.sha256"
LOCAL_TAR="${DOWNLOAD_DIR}/${TAR_FILE}"
LOCAL_SHA="${DOWNLOAD_DIR}/${SHA_FILE}"

URL_BASE="${GITLAB_URL}/api/v4/projects/${PROJECT_ID}/packages/generic/${PACKAGE_NAME}/${PACKAGE_VERSION}"

# ============================================================================
# 令牌 (支持环境变量覆盖，未设置时使用内置默认值)
# ============================================================================
DEFAULT_TOKEN="glpat-4uOHXjtNm9cbBGsWY797oG86MQp1OjJyCA.01.0y16v91v7"
TOKEN="${GITLAB_TOKEN:-${DEFAULT_TOKEN}}"

validate_token() {
    if [[ ! "$TOKEN" =~ ^glpat- ]]; then
        log_warn "GITLAB_TOKEN 格式异常(期望 glpat- 前缀), 将继续尝试下载"
    fi
}

# ============================================================================
# 版本缓存检查
# ============================================================================
check_version_cache() {
    if [[ "$FORCE" == true ]]; then
        log_info "强制模式，跳过版本缓存检查"
        return 1
    fi

    if [[ ! -f "$VERSION_FILE" ]]; then
        return 1
    fi

    local cached_ver
    cached_ver="$(cat "$VERSION_FILE" 2>/dev/null)"
    local expected="${PACKAGE_VERSION}-${TARGET_ARCH}"

    if [[ "$cached_ver" != "$expected" ]]; then
        log_info "版本不匹配 (缓存: ${cached_ver}, 期望: ${expected}), 重新下载"
        return 1
    fi

    # 验证关键目录存在
    if [[ ! -d "${INSTALL_DIR}/include" || ! -d "${INSTALL_DIR}/lib" ]]; then
        log_warn "版本缓存有效但目录不完整，重新下载"
        return 1
    fi

    log_success "预编译包已就绪 (${COMPONENT} @ ${expected})"
    log_info "  include: ${INSTALL_DIR}/include/"
    log_info "  lib:     ${INSTALL_DIR}/lib/"
    return 0
}

# ============================================================================
# 文件下载 (支持重试)
# ============================================================================
download_file() {
    local remote_file="$1"
    local local_dest="$2"
    local url="${URL_BASE}/${remote_file}"
    local max_retries=3
    local retry_delay=2

    log_info "下载: ${remote_file}"

    for ((i=1; i<=max_retries; i++)); do
        local http_code
        http_code=$(curl --location --silent --show-error --fail --insecure \
            --header "PRIVATE-TOKEN: ${TOKEN}" \
            --output "$local_dest" \
            --write-out "%{http_code}" \
            "$url" 2>/dev/null) || true

        if [[ "$http_code" == "200" ]]; then
            log_success "下载成功: ${remote_file} (${i}/${max_retries})"
            return 0
        fi

        if [[ $i -lt $max_retries ]]; then
            log_warn "下载失败 (HTTP ${http_code}), ${retry_delay}s 后重试 (${i}/${max_retries})..."
            sleep "$retry_delay"
            retry_delay=$((retry_delay * 2))
        else
            log_error "下载失败: ${remote_file} (HTTP ${http_code})"
            return 1
        fi
    done
}

# ============================================================================
# SHA256 校验
# ============================================================================
verify_sha256() {
    log_info "校验 SHA256 完整性..."

    if [[ ! -f "$LOCAL_TAR" ]]; then
        log_error "tar 包不存在: ${LOCAL_TAR}"
        return 1
    fi

    if [[ ! -f "$LOCAL_SHA" ]]; then
        log_error "SHA256 文件不存在: ${LOCAL_SHA}"
        return 1
    fi

    local expected_hash actual_hash
    expected_hash="$(awk '{print $1}' "$LOCAL_SHA" | tr -d '[:space:]')"
    actual_hash="$(sha256sum "$LOCAL_TAR" | awk '{print $1}')"

    if [[ "$expected_hash" != "$actual_hash" ]]; then
        log_error "SHA256 校验失败!"
        log_error "  期望: ${expected_hash}"
        log_error "  实际: ${actual_hash}"
        return 1
    fi

    log_success "SHA256 校验通过"
}

# ============================================================================
# 解压安装
# ============================================================================
extract_and_install() {
    log_info "解压预编译包到: ${INSTALL_DIR}"

    # 清空旧内容
    if [[ -d "$INSTALL_DIR" ]]; then
        rm -rf "$INSTALL_DIR"
    fi
    mkdir -p "$INSTALL_DIR"

    # 先解压到临时目录，再处理 strip-components
    local tmp_dir
    tmp_dir="$(mktemp -d)"
    trap "rm -rf '${tmp_dir}'" RETURN

    if ! tar -xzf "$LOCAL_TAR" -C "$tmp_dir"; then
        log_error "解压失败: ${LOCAL_TAR}"
        return 1
    fi

    if [[ "$STRIP_COMPONENTS" -gt 0 ]]; then
        # 剥除顶层目录
        local top_entry
        top_entry="$(ls -A "$tmp_dir" | head -1)"
        if [[ -n "$top_entry" && -d "${tmp_dir}/${top_entry}" ]]; then
            cp -a "${tmp_dir}/${top_entry}/." "$INSTALL_DIR/"
        else
            cp -a "${tmp_dir}/." "$INSTALL_DIR/"
        fi
    else
        cp -a "${tmp_dir}/." "$INSTALL_DIR/"
    fi

    log_success "解压安装完成"
}

# ============================================================================
# 安装后验证
# ============================================================================
verify_installation() {
    log_info "验证安装完整性..."
    local errors=0

    if [[ ! -d "${INSTALL_DIR}/include" ]]; then
        log_error "缺少 include 目录: ${INSTALL_DIR}/include/"
        ((errors++))
    fi

    if [[ ! -d "${INSTALL_DIR}/lib" ]]; then
        log_error "缺少 lib 目录: ${INSTALL_DIR}/lib/"
        ((errors++))
    fi

    case "$COMPONENT" in
        qifeng_framework)
            local fw_lib="${INSTALL_DIR}/lib/qifeng_framework/libqifeng.so"
            if [[ -f "$fw_lib" ]]; then
                log_success "qifeng_framework: libqifeng.so 就绪"
            else
                log_error "qifeng_framework: 缺少 libqifeng.so"
                ((errors++))
            fi
            ;;
    esac

    return $errors
}

# ============================================================================
# 主流程
# ============================================================================
main() {
    log_header
    echo -e "${CYAN}  预编译依赖下载器${NC}"
    echo -e "${CYAN}  组件: ${COMPONENT}  版本: ${PACKAGE_VERSION}  架构: ${TARGET_ARCH}${NC}"
    log_header

    # 1. 令牌校验
    validate_token || exit 1

    # 2. 版本缓存检查
    if check_version_cache; then
       exit 0
    fi

    # 3. 准备下载目录
    mkdir -p "$DOWNLOAD_DIR"

    # 4. 清理旧下载缓存
    rm -f "$LOCAL_TAR" "$LOCAL_SHA"

    # 5. 下载 tar 包与 SHA256 文件
    download_file "$TAR_FILE" "$LOCAL_TAR" || exit 1
    download_file "$SHA_FILE" "$LOCAL_SHA" || exit 1

    # 6. SHA256 完整性校验
    verify_sha256 || exit 1

    # 7. 解压安装
    extract_and_install || exit 1

    # 8. 安装后验证
    verify_installation || {
        log_error "安装完整性检查失败，构建将不可用"
        exit 1
    }

    # 9. 写入版本文件
    echo "${PACKAGE_VERSION}-${TARGET_ARCH}" > "$VERSION_FILE"
    log_info "版本标记已写入: ${VERSION_FILE}"

    # 10. 保留下载的 tar 包在 download/ 中，便于离线重装
    log_info "tar 包保留在: ${LOCAL_TAR}"

    log_header
    log_success "依赖安装完成: ${COMPONENT} @ ${PACKAGE_VERSION}-${TARGET_ARCH}"
    log_info "  路径: ${INSTALL_DIR}"
    log_header
}

# ============================================================================
# 全局错误处理
# ============================================================================
on_error() {
    local exit_code=$?
    local line_no=$1
    local command="$2"
    log_error "脚本异常退出 (行: ${line_no}, 命令: ${command}, 退出码: ${exit_code})"
    exit "$exit_code"
}

trap 'on_error $LINENO "$BASH_COMMAND"' ERR

main
