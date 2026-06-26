#!/bin/bash
# ==============================================================================
# build.sh - 拉取 pugixml 源码并编译为静态库
#
# pugixml 是轻量级 C++ XML 处理库
# 许可证：MIT License（允许免费闭源商用）
#
# 用法：
#   ./build.sh
#
# 产物目录结构：
#   third_party/pugixml/
#   ├── include/
#   │   ├── pugixml.hpp
#   │   └── pugiconfig.hpp
#   └── lib/
#       └── libpugixml.a
#
# 依赖：C++17 编译器（g++ >= 7）
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
INCLUDE_DIR="${SCRIPT_DIR}/include"
LIB_DIR="${SCRIPT_DIR}/lib"

echo "=== pugixml 构建脚本 ==="

# ─── 清理旧产物 ───
rm -rf "${SRC_DIR}" "${INCLUDE_DIR}" "${LIB_DIR}"
mkdir -p "${SRC_DIR}" "${INCLUDE_DIR}" "${LIB_DIR}"

# ─── 拉取源码 ───
# pugixml 源码仅 3 个文件，直接下载单文件
# 优先 GitHub，失败回退 Gitee 镜像
PUGIXML_FILES=(pugixml.cpp pugixml.hpp pugiconfig.hpp)

echo "[1/3] 拉取 pugixml 源码..."

GITHUB_BASE="https://raw.githubusercontent.com/zeux/pugixml/master/src"
GITEE_BASE="https://gitee.com/mirrors/pugixml/raw/master/src"

download_success=false
for base_url in "${GITHUB_BASE}" "${GITEE_BASE}"; do
    echo "  尝试: ${base_url}"
    all_ok=true
    for f in "${PUGIXML_FILES[@]}"; do
        if curl -sL --connect-timeout 10 "${base_url}/${f}" -o "${SRC_DIR}/${f}" 2>/dev/null; then
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
    echo "错误: 无法从 GitHub 或 Gitee 拉取 pugixml 源码"
    exit 1
fi

# ─── 编译 ───
echo "[2/3] 编译 pugixml..."

cd "${SRC_DIR}"
echo "  编译 pugixml.cpp..."
g++ -std=c++17 -O2 -Wall -fPIE -c -o pugixml.o pugixml.cpp

echo "  打包 libpugixml.a..."
ar rcs "${LIB_DIR}/libpugixml.a" pugixml.o

# ─── 安装头文件 ───
echo "[3/3] 安装头文件..."
cp "${SRC_DIR}/pugixml.hpp" "${INCLUDE_DIR}/"
cp "${SRC_DIR}/pugiconfig.hpp" "${INCLUDE_DIR}/"

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
echo "pugixml 构建成功！"
