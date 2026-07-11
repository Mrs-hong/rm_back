/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/scmd_types.h"
#include "common/types.h"
#include "service_manger/service_context.h"

#include <string>

namespace qifeng::scm {

    /**
     * @brief 数据库服务管理器
     * @details 从 ServiceControl 和 UpgradeService 中提取的数据库逻辑统一管理。
     *          负责服务数据库初始化（安装时）、数据库用户管理、SQL 脚本执行、
     *          升级期数据库备份/恢复以及卸载时数据库清理。
     *          通过 ServiceContext 共享 ConfigLoader/FileManager 依赖，
     *          底层使用 service_tool::Mariadb 执行具体数据库操作。
     */
    class DatabaseService {
    public:
        /**
         * @brief 构造函数
         * @param ctx 共享依赖上下文（需由调用方保活）
         */
        explicit DatabaseService(const ServiceContext& ctx);

        ~DatabaseService();

        DatabaseService(const DatabaseService&) = delete;
        DatabaseService& operator=(const DatabaseService&) = delete;
        DatabaseService(DatabaseService&&) = delete;
        DatabaseService& operator=(DatabaseService&&) = delete;

        // === 服务数据库初始化（安装时调用） ===

        /**
         * @brief 初始化服务的数据库
         * @details 为服务创建数据库用户、写密码文件、执行 sqlDir 下初始化脚本。
         *          任一步骤失败则回滚（删除用户、清除密码文件）。
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg InitServiceDatabase(const std::string& serviceName);

        // === 数据库用户管理 ===

        /**
         * @brief 创建数据库用户
         * @param dbType 数据库类型
         * @param username 用户名
         * @param password 密码
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateDatabaseUser(const DatabaseType& dbType, const std::string& username,
                                     const std::string& password);

        /**
         * @brief 删除数据库用户及其所有数据库
         * @param dbType 数据库类型
         * @param username 用户名
         * @param password 用户密码（可选，提供时可发现更多数据库）
         * @return ResultMsg 操作结果
         */
        ResultMsg DeleteDatabaseUser(const DatabaseType& dbType, const std::string& username,
                                     const std::string& password = "");

        /**
         * @brief 执行数据库初始化脚本
         * @details 执行 sqlDir 下所有 .sql 脚本（按文件名排序）
         * @param dbType 数据库类型
         * @param sqlDir SQL 脚本目录路径
         * @param dbUserName 数据库用户名（可选）
         * @param dbPassword 数据库密码（可选）
         * @return ResultMsg 操作结果
         */
        ResultMsg ExecuteDbInitScripts(const DatabaseType& dbType, const std::string& sqlDir,
                                       const std::string& dbUserName = "", const std::string& dbPassword = "");

        // === 升级期数据库备份/恢复 ===

        /**
         * @brief 备份服务关联的数据库（升级前调用）
         * @details 当服务依赖 mariadb 且 service.yaml 含 initDB_sql_dir 时，
         *          备份服务用户拥有的所有数据库到 backupDir/<serviceName>/db_backup/
         * @param serviceName 服务名称
         * @return ResultMsg 成功时 msg 为备份目录路径；无需备份时返回成功
         */
        ResultMsg BackupServiceDatabase(const std::string& serviceName);

        /**
         * @brief 执行升级用 SQL 脚本
         * @details 当 service.yaml 含 initDB_sql_dir 时，以服务账号身份按字母序执行该目录下所有 .sql 脚本
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg ExecuteUpgradeScripts(const std::string& serviceName);

        /**
         * @brief 回退数据库（升级失败回滚时调用）
         * @details 从 backupDir/<serviceName>/db_backup/ 恢复数据库
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg RollbackServiceDatabase(const std::string& serviceName);

        // === 清理 ===

        /**
         * @brief 清除服务关联的数据库数据
         * @details 从密码文件读取用户密码，以用户身份发现所有可访问数据库后删除用户及其数据库
         * @param svc 服务定义（需包含 dbInfo 和 currentServiceDir）
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearDatabaseData(const ServiceDefinition& svc);

    private:
        /**
         * @brief 写入数据库用户密码到文件
         * @param svc 服务定义
         * @param dbUserName 数据库用户名
         * @param dbPassword 数据库密码
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateDbUserPassword(const ServiceDefinition& svc, const std::string& dbUserName,
                                       const std::string& dbPassword);

        /**
         * @brief 清除数据库用户密码文件
         * @param svc 服务定义
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearDbUserPasswordFile(const ServiceDefinition& svc);

        /**
         * @brief 获取服务依赖的数据库服务名
         * @param serviceName 服务名称
         * @return std::string 数据库服务名，无依赖时返回空字符串
         */
        std::string GetDependentDatabaseServiceName(const std::string& serviceName);

        const ServiceContext& mCtx;
    };

}  // namespace qifeng::scm
