/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/scmd_types.h"
#include "common/types.h"

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
         * @brief 判断服务是否处于活跃（运行中）状态
         * @details 轻量级查询，仅通过 DBus 获取 systemd ActiveState，不收集完整运行时信息
         * @param serviceName 服务名称
         * @return bool true 表示服务处于 active 状态
         */
        bool IsServiceActive(const std::string &serviceName);

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
         * @param username 用户名
         * @param password 密码
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateDatabaseUser(const DatabaseType &dbType, const std::string &username,
                                     const std::string &password);

        /**
         * @brief 删除数据库用户
         * @details 在指定服务的数据库中删除指定用户，需在数据库启动后调用
         * @param dbServiceName 数据库服务名称
         * @param username 用户名
         * @return ResultMsg 操作结果
         */
        ResultMsg DeleteDatabaseUser(const DatabaseType &dbType, const std::string &username);

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

        /**
         * @brief 获取服务的 journal 日志（用于 SLOG 命令）
         * @details 通过 sd-journal API 读取指定服务最近 logCount 条日志，等价于
         *          journalctl -u <unit>.service -n <count>，返回日志原文（每条一行）
         * @param serviceName 服务名称
         * @param logCount 日志行数（<=0 时使用默认 10）
         * @return ResultMsg 成功时 msg 为 journal 日志原文
         */
        ResultMsg GetServiceJournal(const std::string &serviceName, int logCount);

    private:
        /**
         * @brief 初始化服务的数据库
         * @details 为服务执行数据库初始化脚本
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg InitServiceDatabase(const std::string &serviceName);

        std::string GetDependentDatabaseServiceName(const std::string &serviceName);

        /**
         * @brief 写入数据库用户密码到文件
         * @details 用于后续服务启动时验证数据库连接
         * @param serviceDefinition 服务定义
         * @param dbUserName 数据库用户名
         * @param dbPassword 数据库密码
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateDbUserPassword(const ServiceDefinition &serviceDefinition, const std::string &dbUserName,
                                       const std::string &dbPassword);

        /**
         * @brief 清除数据库用户密码文件
         * @details 用于在服务卸载时删除数据库用户密码文件
         * @param serviceDefinition 服务定义
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearDbUserPasswordFile(const ServiceDefinition &serviceDefinition);

        /**
         * @brief 为服务执行数据库初始化脚本
         * @details 从指定目录执行所有SQL脚本
         * @param dbServiceName 数据库服务名称
         * @param sqlDir SQL初始化脚本目录路径
         * @param dbUserName 数据库用户名
         * @param dbPassword 数据库密码
         * @return ResultMsg 操作结果
         */
        ResultMsg ExecuteDbInitScripts(const DatabaseType &dbType, const std::string &sqlDir,
                                       const std::string &dbUserName = "", const std::string &dbPassword = "");

        /**
         * @brief 清除服务数据
         * @details 尝试删除服务所有关联文件
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearServiceData(const std::string &serviceName);

        /**
         * @brief 清除数据库数据
         * @details 尝试删除服务所有关联数据库用户、数据库等
         * @param dbServiceName 数据库服务名称
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearDatabaseData(const DatabaseType &dbType, const std::string &serviceName);

    private:
        bool mIsInit {false};                             // 是否初始化
        ServiceRuntimeInfo mServiceRuntimeInfo;           // 服务运行时信息
        std::shared_ptr<ConfigLoader> mConfigLoader;      // 配置加载器
        std::shared_ptr<ServiceManager> mServiceManager;  // 服务管理器
    };
}  // namespace qifeng::scm
