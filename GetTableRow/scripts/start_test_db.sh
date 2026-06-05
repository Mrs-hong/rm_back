#!/bin/bash
# ============================================
# 启动测试数据库脚本
# 功能：启动独立的 MySQL 实例用于测试
# 用法：bash scripts/start_test_db.sh
# ============================================

set -e

PROJECT_ROOT="/home/lyh/code/test_p/GetTableRow"
DB_DATA="${PROJECT_ROOT}/db_data"

echo "[信息] 启动测试 MySQL 实例..."

# 检查是否已运行
if [ -f "${DB_DATA}/mysql.pid" ] && kill -0 "$(cat ${DB_DATA}/mysql.pid)" 2>/dev/null; then
    echo "[信息] MySQL 测试实例已在运行 (PID: $(cat ${DB_DATA}/mysql.pid))"
    exit 0
fi

# 启动 mysqld
mysqld --defaults-file="${DB_DATA}/my.cnf" --daemonize

sleep 2

# 检查是否启动成功
if [ -f "${DB_DATA}/mysql.pid" ] && kill -0 "$(cat ${DB_DATA}/mysql.pid)" 2>/dev/null; then
    echo "[信息] MySQL 测试实例启动成功，端口 3307"
else
    echo "[错误] MySQL 测试实例启动失败"
    exit 1
fi
