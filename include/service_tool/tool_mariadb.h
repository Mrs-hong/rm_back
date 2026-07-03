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
         *
         * @param user 用户名
         * @return ResultMsg 操作结果
         */
        ResultMsg DeleteUserAndDatabase(const std::string &user);
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

    private:
        MariadbDef mDef;  /// mariadb服务配置信息
    };
}  // namespace qifeng::scm::tool