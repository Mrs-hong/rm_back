#!/bin/bash
# ============================================
# 停止测试数据库脚本
# 用法：bash scripts/stop_test_db.sh
# ============================================

DB_DATA="/home/lyh/code/test_p/GetTableRow/db_data"

if [ -f "${DB_DATA}/mysql.pid" ]; then
    PID=$(cat "${DB_DATA}/mysql.pid")
    if kill -0 "$PID" 2>/dev/null; then
        echo "[信息] 停止 MySQL 测试实例 (PID: $PID)..."
        mysqladmin -S "${DB_DATA}/mysql.sock" -u root shutdown
    else
        echo "[信息] MySQL 测试实例未运行"
        rm -f "${DB_DATA}/mysql.pid"
    fi
else
    echo "[信息] 未找到 PID 文件"
fi
