#!/bin/bash
# ============================================
# 编译脚本
# 功能：自动创建构建目录并编译项目
# 用法：bash scripts/build.sh
# ============================================

set -e

# 获取脚本所在目录及项目根目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build"

echo "========================================"
echo "  GetTableRow 项目编译脚本"
echo "========================================"

# 检查 CMake 是否安装
if ! command -v cmake &> /dev/null; then
    echo "[错误] 未找到 cmake，请先安装 CMake。"
    exit 1
fi

# 检查 mysql_config 是否可用（用于查找 MySQL 库）
if ! command -v mysql_config &> /dev/null; then
    echo "[错误] 未找到 mysql_config，请先安装 libmysqlclient-dev。"
    exit 1
fi

# 创建并进入构建目录
echo "[信息] 创建构建目录: ${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# 运行 CMake 配置
echo "[信息] 运行 CMake 配置..."
cmake "${PROJECT_ROOT}" -DCMAKE_BUILD_TYPE=Release

# 编译项目
echo "[信息] 开始编译..."
cmake --build . --parallel "$(nproc 2>/dev/null || echo 1)"

echo "========================================"
echo "  编译成功！"
echo "  可执行文件: ${BUILD_DIR}/get_table_row"
echo "========================================"
