#!/bin/bash
# ==============================================================================
# build.sh - 拉取 minizip 源码并编译为静态库
#
# minizip 是 zlib contrib 中的 ZIP 读写库
# 许可证：zlib License（允许免费闭源商用）
#
# 用法：
#   ./build.sh
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
# 依赖：zlib（系统安装 zlib1g-dev）
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
INCLUDE_DIR="${SCRIPT_DIR}/include"
LIB_DIR="${SCRIPT_DIR}/lib"

echo "=== minizip 构建脚本 ==="

# ─── 清理旧产物 ───
rm -rf "${SRC_DIR}" "${INCLUDE_DIR}" "${LIB_DIR}"
mkdir -p "${SRC_DIR}" "${INCLUDE_DIR}" "${LIB_DIR}"

# ─── 拉取源码 ───
# 优先从 GitHub 拉取，失败则回退到 Gitee 镜像
# 使用稳定版 v1.3.1（master 分支有额外依赖如 skipset.h）
MINIZIP_FILES=(zip.c zip.h unzip.c unzip.h ioapi.c ioapi.h crypt.h)

echo "[1/3] 拉取 minizip 源码..."

GITHUB_BASE="https://raw.githubusercontent.com/madler/zlib/v1.3.1/contrib/minizip"
GITEE_BASE="https://gitee.com/mirrors/zlib/raw/v1.3.1/contrib/minizip"

download_success=false
for base_url in "${GITHUB_BASE}" "${GITEE_BASE}"; do
    echo "  尝试: ${base_url}"
    all_ok=true
    for f in "${MINIZIP_FILES[@]}"; do
        if curl -sL --connect-timeout 10 "${base_url}/${f}" -o "${SRC_DIR}/${f}" 2>/dev/null; then
            # 检查文件是否有效（非空且不是 HTML 错误页）
            if [ -s "${SRC_DIR}/${f}" ] && ! head -1 "${SRC_DIR}/${f}" | grep -q "<!DOCTYPE\|<html\|404\|Not Found"; then
                echo "    OK: ${f}"
            else
                echo "    失败: ${f} (无效内容)"
                all_ok=false
                break
            fi
        else
            echo "    失败: ${f}"
            all_ok=false
            break
        fi
    done
    if [ "${all_ok}" = true ]; then
        download_success=true
        break
    fi
done

if [ "${download_success}" = false ]; then
    echo "错误: 无法从 GitHub 或 Gitee 拉取 minizip 源码"
    exit 1
fi

# ─── 编译 ───
echo "[2/3] 编译 minizip..."

# 获取 zlib 头文件路径
ZLIB_INCLUDE=$(pkg-config --cflags-only-I zlib 2>/dev/null || echo "")
if [ -z "$ZLIB_INCLUDE" ]; then
    # 回退：直接使用系统默认路径
    ZLIB_INCLUDE="-I/usr/include"
fi

cd "${SRC_DIR}"

# 编译每个 .c 文件为 .o
for f in zip.c unzip.c ioapi.c; do
    echo "  编译 ${f}..."
    gcc -c -O2 -Wall -fPIE ${ZLIB_INCLUDE} -o "${f%.c}.o" "${f}"
done

# 打包为静态库
echo "  打包 libminizip.a..."
ar rcs "${LIB_DIR}/libminizip.a" zip.o unzip.o ioapi.o

# ─── 安装头文件 ───
echo "[3/3] 安装头文件..."
cp "${SRC_DIR}/zip.h" "${INCLUDE_DIR}/"
cp "${SRC_DIR}/unzip.h" "${INCLUDE_DIR}/"
cp "${SRC_DIR}/ioapi.h" "${INCLUDE_DIR}/"

# ─── 验证 ───
echo ""
echo "=== 构建完成 ==="
echo "头文件: ${INCLUDE_DIR}/"
ls -la "${INCLUDE_DIR}/"
echo "静态库: ${LIB_DIR}/"
ls -la "${LIB_DIR}/"

# 清理中间文件
rm -f "${SRC_DIR}"/*.o

echo ""
echo "minizip 构建成功！"
