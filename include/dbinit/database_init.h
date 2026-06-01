/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "dbinit/database_backend.h"
#include <memory>
#include <string>

namespace qifeng {
    namespace scm {

        /**
         * @brief 数据库初始化管理器
         * 提供数据库的初始化、启停、用户/数据库/表管理等统一接口
         * 内部根据 DatabaseType 委托给具体的数据库后端实现
         */
        class DatabaseInit {
        public:
            /**
             * @brief 构造函数
             * @param config 数据库初始化配置
             */
            explicit DatabaseInit(const DatabaseConfig &config);

            ~DatabaseInit() = default;
            DatabaseInit(const DatabaseInit &) = delete;
            DatabaseInit &operator=(const DatabaseInit &) = delete;
            DatabaseInit(DatabaseInit &&) = delete;
            DatabaseInit &operator=(DatabaseInit &&) = delete;

            /**
             * @brief 首次初始化数据库，创建root用户
             * @return ResultMsg 初始化结果
             */
            ResultMsg Initialize();

            /**
             * @brief 启动数据库
             * @return ResultMsg 启动结果
             */
            ResultMsg Start();

            /**
             * @brief 关闭数据库
             * @return ResultMsg 关闭结果
             */
            ResultMsg Stop();

            /**
             * @brief 检查数据库是否运行中
             * @return bool 是否运行中
             */
            bool IsRunning() const;

            /**
             * @brief 创建数据库用户
             * @param username 用户名
             * @param password 密码
             * @return ResultMsg 创建结果
             */
            ResultMsg CreateUser(const std::string &username, const std::string &password);

            /**
             * @brief 删除数据库用户
             * @param username 用户名
             * @return ResultMsg 删除结果
             */
            ResultMsg DeleteUser(const std::string &username);

            /**
             * @brief 创建数据库
             * @param dbName 数据库名
             * @return ResultMsg 创建结果
             */
            ResultMsg CreateDatabase(const std::string &dbName);

            /**
             * @brief 删除数据库
             * @param dbName 数据库名
             * @return ResultMsg 删除结果
             */
            ResultMsg DeleteDatabase(const std::string &dbName);

            /**
             * @brief 创建表
             * @param dbName 数据库名
             * @param tableName 表名
             * @param columns 列定义（如 "id INT PRIMARY KEY, name VARCHAR(100)"）
             * @return ResultMsg 创建结果
             */
            ResultMsg CreateTable(const std::string &dbName, const std::string &tableName, const std::string &columns);

            /**
             * @brief 删除表
             * @param dbName 数据库名
             * @param tableName 表名
             * @return ResultMsg 删除结果
             */
            ResultMsg DropTable(const std::string &dbName, const std::string &tableName);

            /**
             * @brief 执行SQL语句
             * @param sql SQL语句
             * @param dbName 目标数据库名（空则使用默认数据库）
             * @return ResultMsg 执行结果
             */
            ResultMsg ExecuteSQL(const std::string &sql, const std::string &dbName = "");

            /**
             * @brief 设置数据存储位置（需在初始化前调用）
             * @param dataDir 新的数据存储目录
             * @return ResultMsg 设置结果
             */
            ResultMsg SetDataDir(const std::string &dataDir);

            /**
             * @brief 执行 SQL 初始化脚本
             * 扫描 dbInfo.sqlDir 目录下所有 .sql 文件，按文件名排序逐个执行
             * 需在数据库启动后调用
             * @return ResultMsg 执行结果
             */
            ResultMsg ExecuteInitScripts();

            /**
             * @brief 获取当前配置
             * @return const DatabaseConfig& 配置引用
             */
            const DatabaseConfig &GetConfig() const;

        private:
            /**
             * @brief 根据 DatabaseType 创建对应的后端实例
             * @param dbType 数据库类型
             * @return std::unique_ptr<IDatabaseBackend> 后端实例
             */
            static std::unique_ptr<IDatabaseBackend> CreateBackend(DatabaseType dbType);

        private:
            DatabaseConfig mConfig;
            std::unique_ptr<IDatabaseBackend> mBackend;
        };

    }  // namespace scm
}  // namespace qifeng
