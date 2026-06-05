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
     * @brief 服务主类：scmd的核心功能入口
     * @details 作为门面类协调 ConfigLoader、ServiceManager、DatabaseInit 三个组件，
     * 提供完整的服务生命周期管理和数据库操作功能。
     * 典型使用流程：Init() → Installed() → InitServiceDataBase() → StartService() → 数据库操作 → UpgradeService() →
     * StopService()->UninstallService()
     */
    class ServiceControl {
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
         *  1. 若指定服务为数据库服务则初始化数据库并创建管理员用户
         *  2. 若是其它服务、若依赖数据库则执行数据库初始化脚本
         * @param serviceName 服务名称
         * @param serviceTarPath 服务tar包路径
         * @return ResultMsg 操作结果
         */
        ResultMsg Installed(const std::string &serviceName, const std::string &serviceTarPath);

        /**
         * @brief 卸载服务
         * @details 停止服务、删除服务文件和目录、从配置中移除
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg UninstallService(const std::string &serviceName);

        /**
         * @brief 升级服务
         * @details 停止运行中的服务、备份旧版本、安装新版本、恢复运行状态。
         * 升级过程中数据目录会被保留。
         * @param serviceName 服务名称
         * @param serviceTarPath 新版本服务tar包路径
         * @return ResultMsg 操作结果
         */
        ResultMsg UpgradeService(const std::string &serviceName, const std::string &serviceTarPath);

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
         * @brief 获取服务运行时结构化信息
         * @details 直接返回 ServiceRuntimeInfo，由调用方决定如何序列化，避免内部 JSON 中转
         * @param serviceName 服务名称
         * @return ServiceRuntimeInfo 服务运行时信息（status 为空表示服务未找到或未初始化）
         */
        ServiceRuntimeInfo GetServiceRuntimeInfo(const std::string &serviceName);

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
         * @brief 创建数据库用户
         * @details 在指定服务的数据库中创建新用户，需在数据库启动后调用
         * @param dbServiceName 数据库服务名称
         * @param username 用户名
         * @param password 密码
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateDatabaseUser(const std::string &dbServiceName, const std::string &username,
                                     const std::string &password);

        /**
         * @brief 创建数据库
         * @details 在使用数据库管理账号、密码登录后创建新数据库，需在数据库启动后调用
         * @param dbServiceName 数据库服务名称
         * @param dbName 数据库名
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateDatabase(const std::string &dbServiceName, const std::string &dbName);

        /**
         * @brief 创建表
         * @details 在使用数据库管理账号、密码登录后创建新表，需在数据库启动后调用
         * @param dbServiceName 数据库服务名称
         * @param dbName 数据库名
         * @param tableName 表名
         * @param columns 列定义（如 "id INT PRIMARY KEY, name VARCHAR(100)"）
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateDatabaseTable(const std::string &dbServiceName, const std::string &dbName,
                                      const std::string &tableName, const std::string &columns);

        /**
         * @brief 执行SQL语句
         * @details 在使用数据库管理账号、密码登录后执行SQL语句，需在数据库启动后调用
         * @param dbServiceName 数据库服务名称
         * @param sql SQL语句
         * @param dbName 目标数据库名（空则使用默认数据库）
         * @return ResultMsg 操作结果
         */
        ResultMsg ExecuteSQL(const std::string &dbServiceName, const std::string &sql, const std::string &dbName = "");

        // === 扩展接口（供ScmServer调用） ===

        /**
         * @brief 获取配置加载器
         * @return const ConfigLoader& 配置加载器引用
         */
        const ConfigLoader &GetConfigLoader() const;

        /**
         * @brief 获取所有服务信息（用于LIST命令）
         * @return ResultMsg 操作结果，成功时msg包含服务列表JSON
         */
        ResultMsg GetAllServicesInfo();

        /**
         * @brief 重启所有服务（用于RESTART_ALL命令）
         * @details 按依赖逆序停止所有运行中服务，再按依赖顺序启动
         * @return ResultMsg 操作结果
         */
        ResultMsg RestartAllServices();

        /**
         * @brief 获取操作日志（用于LOG命令）
         * @param logLevel 日志级别过滤
         * @param logCount 日志条数限制
         * @return ResultMsg 操作结果，成功时data包含日志内容
         */
        ResultMsg GetOperationLog(int logLevel, int logCount);

        // 根据服务的类型或者对数据库的依赖初始化数据库
        /**
         * @brief 根据服务的类型或者对数据库的依赖初始化数据库
         * @details 初次初始化或执行数据库脚本
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg InitDataBase(const std::string &serviceName);

    private:
        /**
         * @brief 根据服务名构建数据库配置
         * @details 从ConfigLoader获取ServiceDefinition，提取数据库类型、二进制路径、数据目录等信息
         * @param dbServiceName 数据库服务名称
         * @return DatabaseConfig 数据库配置
         */
        DatabaseConfig BuildDatabaseConfig(const std::string &dbServiceName, bool isInit = false);

        /**
         * @brief 首次初始化服务数据库
         * @details 创建管理员用户、系统表，并执行指定目录下的SQL初始化脚本。
         * 需在服务安装后、启动前调用。
         * @param dbServiceName 数据库服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg InitDataBaseService(const std::string &dbServiceName);

        /**
         * @brief 为服务执行数据库初始化脚本
         * @details 从指定目录执行所有SQL脚本，需在数据库启动后调用
         * @param dbServiceName 数据库服务名称
         * @param sqlDir SQL初始化脚本目录路径
         * @return ResultMsg 操作结果
         */
        ResultMsg ExecuteDbInitScripts(const std::string &dbServiceName, const std::string &sqlDir);

    private:
        bool mIsInit {false};                             // 是否初始化
        ServiceRuntimeInfo mServiceRuntimeInfo;           // 服务运行时信息
        std::shared_ptr<ConfigLoader> mConfigLoader;      // 配置加载器
        std::shared_ptr<ServiceManager> mServiceManager;  // 服务管理器
    };
}  // namespace qifeng::scm
