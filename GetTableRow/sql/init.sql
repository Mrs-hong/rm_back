-- ============================================
-- 数据库初始化脚本
-- 创建测试数据库和表，并插入示例数据
-- 使用方法: mysql -u root -p < init.sql
-- ============================================

-- 创建数据库（如果不存在则创建）
CREATE DATABASE IF NOT EXISTS test_db
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

-- 切换到目标数据库
USE test_db;

-- 指定自定义表数据存储路径
SET GLOBAL innodb_file_per_table = ON;

-- 删除旧表（若存在），以便重新创建并指定新路径
DROP TABLE IF EXISTS test_table;

-- 创建测试表，并指定 DATA DIRECTORY 将表数据存储到自定义路径
CREATE TABLE test_table (
    id INT AUTO_INCREMENT PRIMARY KEY COMMENT '自增主键ID',
    name VARCHAR(100) NOT NULL COMMENT '名称',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='行数查询测试表'
DATA DIRECTORY = '/home/lyh/code/test_p/GetTableRow/db_storage';

-- 清空表（确保每次初始化数据一致）
TRUNCATE TABLE test_table;

-- 插入测试数据
INSERT INTO test_table (name, created_at) VALUES
    ('测试数据-1', NOW()),
    ('测试数据-2', NOW()),
    ('测试数据-3', NOW()),
    ('测试数据-4', NOW()),
    ('测试数据-5', NOW()),
    ('测试数据-6', NOW()),
    ('测试数据-7', NOW()),
    ('测试数据-8', NOW()),
    ('测试数据-9', NOW()),
    ('测试数据-10', NOW());

-- 查看当前表行数
SELECT COUNT(*) AS total_rows FROM test_table;
