#!/bin/bash
# ============================================
# 打包脚本
# 功能：将编译产物、SQL 脚本、README 打包为 tar.gz
# 用法：bash scripts/package.sh
# ============================================

set -e

# 获取项目根目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build"
PACKAGE_NAME="GetTableRow"
PACKAGE_DIR="${PROJECT_ROOT}/${PACKAGE_NAME}"
OUTPUT_TAR="${PROJECT_ROOT}/${PACKAGE_NAME}.tar.gz"

echo "========================================"
echo "  GetTableRow 打包脚本"
echo "========================================"

# 检查可执行文件是否存在
if [ ! -f "${BUILD_DIR}/get_table_row" ]; then
    echo "[错误] 未找到编译产物: ${BUILD_DIR}/get_table_row"
    echo "[提示] 请先运行 bash scripts/build.sh 完成编译。"
    exit 1
fi

# 清理旧打包目录和旧压缩包
rm -rf "${PACKAGE_DIR}" "${OUTPUT_TAR}"

# 创建打包目录结构
echo "[信息] 创建打包目录..."
mkdir -p "${PACKAGE_DIR}/bin"
mkdir -p "${PACKAGE_DIR}/sql"
mkdir -p "${PACKAGE_DIR}/scripts"

# 复制可执行文件
cp "${BUILD_DIR}/get_table_row" "${PACKAGE_DIR}/bin/"

# 复制 SQL 脚本
cp "${PROJECT_ROOT}/sql/init.sql" "${PACKAGE_DIR}/sql/"

# 复制编译和打包脚本（方便用户二次构建）
cp "${PROJECT_ROOT}/scripts/build.sh" "${PACKAGE_DIR}/scripts/"
cp "${PROJECT_ROOT}/scripts/package.sh" "${PACKAGE_DIR}/scripts/"

# 复制 CMakeLists.txt（方便用户二次构建）
cp "${PROJECT_ROOT}/CMakeLists.txt" "${PACKAGE_DIR}/"

# 复制源码（方便用户二次构建）
cp -r "${PROJECT_ROOT}/src" "${PACKAGE_DIR}/"

# 生成简易 README
cat > "${PACKAGE_DIR}/README.txt" << 'EOF'
========================================
  GetTableRow 定时数据库查询服务
========================================

1. 简介
   本程序每 5 秒连接本地 MySQL（端口 3306），查询 test_db.test_table 表行数并输出日志。

2. 快速开始
   a) 初始化数据库：
      mysql -u root -p < sql/init.sql

   b) 运行程序：
      ./bin/get_table_row

   c) 停止程序：
      按 Ctrl+C 或发送 SIGTERM 信号。

3. 重新编译
   bash scripts/build.sh

4. 重新打包
   bash scripts/package.sh

========================================
EOF

# 生成 tar.gz 压缩包
echo "[信息] 生成压缩包: ${OUTPUT_TAR}"
tar -czf "${OUTPUT_TAR}" -C "${PROJECT_ROOT}" "${PACKAGE_NAME}"

# 清理临时目录
rm -rf "${PACKAGE_DIR}"

echo "========================================"
echo "  打包成功！"
echo "  输出文件: ${OUTPUT_TAR}"
echo "========================================"
