/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "dbinit/database_backend.h"
#include <string>

namespace qifeng {
    namespace scm {

        /**
         * @brief OpenGauss 数据库后端实现
         * 实现 IDatabaseBackend 接口，提供 OpenGauss 数据库的初始化、启停、用户/数据库/表管理等功能
         */
        class OpenGaussBackend : public IDatabaseBackend {
        public:
            OpenGaussBackend() = default;
            ~OpenGaussBackend() override = default;
            OpenGaussBackend(const OpenGaussBackend&) = delete;
            OpenGaussBackend& operator=(const OpenGaussBackend&) = delete;
            OpenGaussBackend(OpenGaussBackend&&) = delete;
            OpenGaussBackend& operator=(OpenGaussBackend&&) = delete;

            ResultMsg Initialize(const DatabaseConfig& config) override;
            ResultMsg Start(const DatabaseConfig& config) override;
            ResultMsg Stop(const DatabaseConfig& config) override;
            bool IsRunning(const DatabaseConfig& config) const override;
            ResultMsg ExecuteSQL(const DatabaseConfig& config, const std::string& sql,
                                  const std::string& dbName) override;
            ResultMsg CreateUser(const DatabaseConfig& config, const std::string& username,
                                  const std::string& password) override;
            ResultMsg DeleteUser(const DatabaseConfig& config, const std::string& username) override;
            ResultMsg CreateDatabase(const DatabaseConfig& config, const std::string& dbName) override;
            ResultMsg DeleteDatabase(const DatabaseConfig& config, const std::string& dbName) override;
            ResultMsg CreateTable(const DatabaseConfig& config, const std::string& dbName,
                                   const std::string& tableName, const std::string& columns) override;
            ResultMsg DropTable(const DatabaseConfig& config, const std::string& dbName,
                                 const std::string& tableName) override;
            ResultMsg SetDataDir(DatabaseConfig& config, const std::string& dataDir) override;

            ResultMsg ExecuteInitScripts(const DatabaseConfig& config) override;

            /**
             * @brief 获取 OpenGauss 默认端口
             * @return int 默认端口号 5432
             */
            static int GetDefaultPort() { return 5432; }

            /**
             * @brief 获取有效端口号（配置为0则返回默认端口）
             * @param config 数据库配置
             * @return int 有效端口号
             */
            static int GetEffectivePort(const DatabaseConfig& config);

        private:
            /**
             * @brief 生成 OpenGauss 配置文件 postgresql.conf
             * @param config 数据库配置
             * @return ResultMsg 生成结果
             */
            ResultMsg GenerateConfig(const DatabaseConfig& config) const;

            /**
             * @brief 获取 gsql 客户端连接命令
             * @param config 数据库配置
             * @param dbName 目标数据库名
             * @return std::string 客户端命令字符串
             */
            static std::string GetGsqlClientCmd(const DatabaseConfig& config, const std::string& dbName);
        };

    }  // namespace scm
}  // namespace qifeng
