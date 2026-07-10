/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once
#include "common/types.h"
#include "tools_def.h"
namespace qifeng::scm::tool {
    /**
     * @brief Mariadb class
     *
     * @details 与系统服务mariadb交互的类、使用cmd命令执行
     */
    class Mariadb {
    public:
        explicit Mariadb(const MariadbDef &def);
        /**
         * @brief 创建用户：具有本地连接和127.0.0.1连接权限以及创建数据库权限、在自己创建数据库所有权限
         *
         * @param user 用户名
         * @param password 密码
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateUser(const std::string &user, const std::string &password);
        /**
         * @brief 删除用户以及所拥有的数据库
         * @details 当提供密码时，以用户身份连接执行 SHOW DATABASES 发现所有可访问数据库（包括无显式 GRANT 的库）；
         *          未提供密码时仅通过 mysql.db 权限表查找。
         *
         * @param user 用户名
         * @param password 用户密码（可选，提供时可发现更多数据库）
         * @return ResultMsg 操作结果
         */
        ResultMsg DeleteUserAndDatabase(const std::string &user, const std::string &password = "");
        /**
         * @brief 执行SQL文件
         *
         * @param sqlFile SQL文件路径
         * @return ResultMsg 操作结果
         */
        ResultMsg ExecuteSqlFile(const std::string &sqlFile);
        /**
         * @brief 执行SQL文件
         *
         * @param user 用户名
         * @param password 密码
         * @param sqlFile SQL文件路径
         * @return ResultMsg 操作结果
         */
        ResultMsg ExecuteSqlFileByUser(const std::string &user, const std::string &password,
                                       const std::string &sqlFilePath);

        /**
         * @brief 获得mariadb服务版本号
         *
         * @return ResultMsg:{0,版本号}, {-1,错误信息}
         */
        ResultMsg GetVersion();

        /**
         * @brief 备份数据库到指定文件（.sql）
         * @param dbName 数据库名
         * @param backupFile 备份文件路径
         * @return ResultMsg 操作结果
         */
        ResultMsg BackupDatabase(const std::string &dbName, const std::string &backupFile);

        /**
         * @brief 从备份文件恢复数据库
         * @param dbName 目标数据库名（不存在会自动创建）
         * @param backupFile 备份文件路径
         * @return ResultMsg 操作结果
         */
        ResultMsg RestoreDatabase(const std::string &dbName, const std::string &backupFile);

        /**
         * @brief 备份用户拥有的所有数据库（按用户名前缀匹配）
         * @param userName 数据库用户名（用于筛选 `<userName>_*` 数据库）
         * @param backupDir 备份目录，每个数据库生成 <dbName>.sql
         * @return ResultMsg 成功时 msg 为已备份的数据库名列表（换行分隔）
         */
        ResultMsg BackupUserDatabases(const std::string &userName, const std::string &backupDir);

        /**
         * @brief 从目录恢复用户所有数据库
         * @param backupDir 备份目录，包含 <dbName>.sql 文件
         * @return ResultMsg 操作结果
         */
        ResultMsg RestoreUserDatabases(const std::string &backupDir);

    private:
        MariadbDef mDef;  /// mariadb服务配置信息
    };
}  // namespace qifeng::scm::tool