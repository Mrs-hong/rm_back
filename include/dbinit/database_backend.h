/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/scmd_types.h"
#include <string>

namespace qifeng {
    namespace scm {

        /**
         * @brief 数据库初始化配置
         * 包含数据库初始化、启动、管理等操作所需的全部配置信息
         */
        struct DatabaseConfig {
            DataBaseInfo dbInfo;    // 数据库基本信息（类型、SQL脚本目录）
            std::string binDir;     // 解压后的二进制包路径（如 /opt/mysql）
            std::string dataDir;    // 数据存储目录（如 /data/mysql）
            int port {0};           // 端口号（0表示使用默认端口：MySQL 3306, OpenGauss 5432）
            std::string adminUser;  // 管理员用户名（MySQL: root, OpenGauss: omm）
            std::string adminPwd;   // 管理员密码
            std::string osUser;     // 运行数据库的系统用户
            std::string osGroup;    // 运行数据库的系统用户组
        };

        /**
         * @brief 数据库后端抽象接口
         * 定义数据库操作的统一接口，由具体数据库后端实现
         * 采用策略模式，支持 MySQL、OpenGauss 等多种数据库后端
         */
        class IDatabaseBackend {
        public:
            IDatabaseBackend() = default;
            virtual ~IDatabaseBackend() = default;
            IDatabaseBackend(const IDatabaseBackend &) = delete;
            IDatabaseBackend &operator=(const IDatabaseBackend &) = delete;
            IDatabaseBackend(IDatabaseBackend &&) = delete;
            IDatabaseBackend &operator=(IDatabaseBackend &&) = delete;

            /**
             * @brief 首次初始化数据库，创建管理员用户
             * @param config 数据库配置
             * @return ResultMsg 初始化结果
             */
            virtual ResultMsg Initialize(const DatabaseConfig &config) = 0;

            /**
             * @brief 启动数据库
             * @param config 数据库配置
             * @return ResultMsg 启动结果
             */
            virtual ResultMsg Start(const DatabaseConfig &config) = 0;

            /**
             * @brief 关闭数据库
             * @param config 数据库配置
             * @return ResultMsg 关闭结果
             */
            virtual ResultMsg Stop(const DatabaseConfig &config) = 0;

            /**
             * @brief 检查数据库是否运行中
             * @param config 数据库配置
             * @return bool 是否运行中
             */
            virtual bool IsRunning(const DatabaseConfig &config) const = 0;

            /**
             * @brief 执行SQL语句
             * @param config 数据库配置
             * @param sql SQL语句
             * @param dbName 目标数据库名（空则使用默认数据库）
             * @return ResultMsg 执行结果
             */
            virtual ResultMsg ExecuteSQL(const DatabaseConfig &config, const std::string &sql,
                                         const std::string &dbName) = 0;

            /**
             * @brief 创建数据库用户
             * @param config 数据库配置
             * @param username 用户名
             * @param password 密码
             * @return ResultMsg 创建结果
             */
            virtual ResultMsg CreateUser(const DatabaseConfig &config, const std::string &username,
                                         const std::string &password) = 0;

            /**
             * @brief 删除数据库用户
             * @param config 数据库配置
             * @param username 用户名
             * @return ResultMsg 删除结果
             */
            virtual ResultMsg DeleteUser(const DatabaseConfig &config, const std::string &username) = 0;

            /**
             * @brief 创建数据库
             * @param config 数据库配置
             * @param dbName 数据库名
             * @return ResultMsg 创建结果
             */
            virtual ResultMsg CreateDatabase(const DatabaseConfig &config, const std::string &dbName) = 0;

            /**
             * @brief 删除数据库
             * @param config 数据库配置
             * @param dbName 数据库名
             * @return ResultMsg 删除结果
             */
            virtual ResultMsg DeleteDatabase(const DatabaseConfig &config, const std::string &dbName) = 0;

            /**
             * @brief 创建表
             * @param config 数据库配置
             * @param dbName 数据库名
             * @param tableName 表名
             * @param columns 列定义（如 "id INT PRIMARY KEY, name VARCHAR(100)"）
             * @return ResultMsg 创建结果
             */
            virtual ResultMsg CreateTable(const DatabaseConfig &config, const std::string &dbName,
                                          const std::string &tableName, const std::string &columns) = 0;

            /**
             * @brief 删除表
             * @param config 数据库配置
             * @param dbName 数据库名
             * @param tableName 表名
             * @return ResultMsg 删除结果
             */
            virtual ResultMsg DropTable(const DatabaseConfig &config, const std::string &dbName,
                                        const std::string &tableName) = 0;

            /**
             * @brief 设置数据存储位置（需在初始化前调用）
             * @param config 数据库配置（会被修改）
             * @param dataDir 新的数据存储目录
             * @return ResultMsg 设置结果
             */
            virtual ResultMsg SetDataDir(DatabaseConfig &config, const std::string &dataDir) = 0;

            /**
             * @brief 执行 SQL 初始化脚本
             * 扫描 config.dbInfo.sqlDir 目录下所有 .sql 文件，按文件名排序逐个执行
             * 需在数据库启动后调用
             * @param config 数据库配置
             * @return ResultMsg 执行结果
             */
            virtual ResultMsg ExecuteInitScripts(const DatabaseConfig &config) = 0;
        };

    }  // namespace scm
}  // namespace qifeng
