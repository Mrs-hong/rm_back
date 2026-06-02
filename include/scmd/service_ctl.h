/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/scmd_types.h"
#include "dbinit/database_backend.h"

#include <memory>
#include <string>

namespace qifeng::scm {
    class ConfigLoader;
    class ServiceManager;

    /**
     * @brief 服务主类：scmd的核心入口
     * @details 作为门面类协调 ConfigLoader、ServiceManager、DatabaseInit 三个组件，
     * 提供完整的服务生命周期管理和数据库操作功能。
     * 典型使用流程：Init() → Installed() → InitServiceDataBase() → StartService() → 数据库操作 → StopService()
     */
    class ServiceMain {
    public:
        // === 服务生命周期管理 ===

        /**
         * @brief 初始化服务主类
         * @details 加载配置、初始化日志系统、创建ServiceManager、启动已安装且autoStart的服务
         * @return ResultMsg 操作结果
         */
        ResultMsg Init();

        /**
         * @brief 安装服务
         * @details 从tar包安装服务：解压、复制到服务目录、注册配置、生成systemd服务文件
         * @param serviceTarPath 服务tar包路径
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg Installed(const std::string &serviceTarPath, const std::string &serviceName);

        /**
         * @brief 卸载服务
         * @details 停止服务、删除服务文件和目录、从配置中移除
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg UninstallService(const std::string &serviceName);

        /**
         * @brief 启动服务
         * @details 通过systemd启动指定服务
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg StartService(const std::string &serviceName);

        /**
         * @brief 停止服务
         * @details 通过systemd停止指定服务
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg StopService(const std::string &serviceName);

        /**
         * @brief 重启服务
         * @details 通过systemd重启指定服务
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg RestartService(const std::string &serviceName);

        /**
         * @brief 重新加载服务配置
         * @details 重新加载指定服务的配置并重新生成systemd服务文件
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg ReloadService(const std::string &serviceName);

        /**
         * @brief 获取服务运行状态
         * @details 获取指定服务的运行时状态信息（JSON格式）
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果，成功时msg为JSON格式状态信息
         */
        ResultMsg GetServiceStatus(const std::string &serviceName);

        /**
         * @brief 启用服务开机自启
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg EnableAutoStart(const std::string &serviceName);

        /**
         * @brief 禁用服务开机自启
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg DisableAutoStart(const std::string &serviceName);

        // === 数据库操作 ===

        /**
         * @brief 首次初始化服务数据库
         * @details 创建管理员用户、系统表，并执行指定目录下的SQL初始化脚本。
         * 需在服务安装后、启动前调用。
         * @param serviceName 服务名称
         * @param sqlDir SQL初始化脚本目录路径
         * @return ResultMsg 操作结果
         */
        ResultMsg InitServiceDataBase(const std::string &serviceName, const std::string &sqlDir);

        /**
         * @brief 创建数据库用户
         * @details 在指定服务的数据库中创建新用户，需在数据库启动后调用
         * @param serviceName 服务名称
         * @param username 用户名
         * @param password 密码
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateDatabaseUser(const std::string &serviceName, const std::string &username,
                                      const std::string &password);

        /**
         * @brief 创建数据库
         * @details 在指定服务的数据库实例中创建新数据库，需在数据库启动后调用
         * @param serviceName 服务名称
         * @param dbName 数据库名
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateServiceDatabase(const std::string &serviceName, const std::string &dbName);

        /**
         * @brief 创建表
         * @details 在指定数据库中创建新表，需在数据库启动后调用
         * @param serviceName 服务名称
         * @param dbName 数据库名
         * @param tableName 表名
         * @param columns 列定义（如 "id INT PRIMARY KEY, name VARCHAR(100)"）
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateDatabaseTable(const std::string &serviceName, const std::string &dbName,
                                       const std::string &tableName, const std::string &columns);

        /**
         * @brief 执行SQL语句
         * @details 在指定服务的数据库中执行SQL语句，需在数据库启动后调用
         * @param serviceName 服务名称
         * @param sql SQL语句
         * @param dbName 目标数据库名（空则使用默认数据库）
         * @return ResultMsg 操作结果
         */
        ResultMsg ExecuteServiceSQL(const std::string &serviceName, const std::string &sql,
                                     const std::string &dbName = "");

    private:
        /**
         * @brief 根据服务名构建数据库配置
         * @details 从ConfigLoader获取ServiceDefinition，提取数据库类型、二进制路径、数据目录等信息
         * @param serviceName 服务名称
         * @return DatabaseConfig 数据库配置
         */
        DatabaseConfig BuildDatabaseConfig(const std::string &serviceName);

    private:
        bool mIsInit {false};                             // 是否初始化
        ServiceRuntimeInfo mServiceRuntimeInfo;           // 服务运行时信息
        std::shared_ptr<ConfigLoader> mConfigLoader;      // 配置加载器
        std::shared_ptr<ServiceManager> mServiceManager;  // 服务管理器
    };
}  // namespace qifeng::scm
