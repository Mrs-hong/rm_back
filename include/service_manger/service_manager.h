/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/scmd_types.h"
#include <memory>
#include <string>
#include <vector>

namespace qifeng::scm {
    class ConfigLoader;
    class FileManager;
    class DBusManager;

    /**
     * @brief 服务管理器
     *
     * 负责协调 ConfigLoader、FileManager、DBusManager 三个组件，
     * 提供完整的服务生命周期管理功能，包括安装、启动、停止、重启、更新、卸载等操作。
     */
    class ServiceManager {
    public:
        /**
         * @brief 构造函数
         * @param configLoader 配置加载器智能指针
         */
        explicit ServiceManager(std::shared_ptr<ConfigLoader> configLoader);

        /**
         * @brief 析构函数
         */
        ~ServiceManager();

        ServiceManager(const ServiceManager &) = delete;
        ServiceManager &operator=(const ServiceManager &) = delete;
        ServiceManager(ServiceManager &&) = delete;
        ServiceManager &operator=(ServiceManager &&) = delete;

        /**
         * @brief 安装服务
         *
         * @param softwareTarPath 软件tar包路径
         * @param serviceName 服务名称（可选，默认从tar包中读取）
         * @return ResultMsg 成功时包含实际服务名称
         */
        ResultMsg InstallService(const std::string &softwareTarPath, const std::string &serviceName);

        /**
         * @brief 启动服务
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg StartService(const std::string &serviceName);

        /**
         * @brief 停止服务
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg StopService(const std::string &serviceName);

        /**
         * @brief 重启服务
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg RestartService(const std::string &serviceName);

        /**
         * @brief 重新加载服务配置
         *
         * @param serviceName 服务名称（可选，空字符串表示重载所有服务）
         * @return ResultMsg
         */
        ResultMsg ReloadService(const std::string &serviceName);

        /**
         * @brief 获取服务运行时信息
         *
         * @param serviceName 服务名称
         * @return ResultMsg 成功时包含 ServiceRuntimeInfo 的序列化字符串
         */
        ResultMsg GetServiceStatus(const std::string &serviceName);

        /**
         * @brief 获取服务运行时详细信息
         *
         * @param serviceName 服务名称
         * @return ServiceRuntimeInfo 服务运行时信息
         */
        ServiceRuntimeInfo GetServiceRuntimeInfo(const std::string &serviceName);

        /**
         * @brief 卸载服务
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg UninstallService(const std::string &serviceName);

        /**
         * @brief 更新服务
         *
         * @param serviceName 服务名称
         * @param softwareTarPath 新软件tar包路径
         * @return ResultMsg
         */
        ResultMsg UpdateService(const std::string &serviceName, const std::string &softwareTarPath);

        /**
         * @brief 启用服务开机自启
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg EnableAutoStart(const std::string &serviceName);

        /**
         * @brief 禁用服务开机自启
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg DisableAutoStart(const std::string &serviceName);

        /**
         * @brief 按依赖顺序启动所有 isAutoStart 的服务
         * @return ResultMsg 操作结果
         */
        ResultMsg StartAllAutoStartServices();

        /**
         * @brief 按依赖逆序停止所有运行中的服务
         * @return ResultMsg 操作结果
         */
        ResultMsg StopAllServices();

        /**
         * @brief 获取当前启动/停止序列
         * @return ServiceSequence 序列
         */
        ServiceSequence GetServiceSequence();

    private:
        /**
         * @brief 解压软件tar包到指定目录并可选指定解压后的文件名
         * @param tarPath tar包路径
         * @param extractDir 输出参数，解压目录路径
         * @param newName 可选，指定解压后的目录名，默认为空字符串表示不处理
         * @return ResultMsg 操作结果
         */
        ResultMsg ExtractSoftwareTar(const std::string &tarPath, std::string &extractDir,
                                     const std::string &newName = "");

        /**
         * @brief 检查服务依赖关系
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg CheckServiceDependencies(const std::string &serviceName);

        /**
         * @brief 启动依赖服务
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg StartDependentServices(const std::string &serviceName);

        /**
         * @brief 初始化服务数据库
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg InitializeServiceDatabase(const std::string &serviceName);

        /**
         * @brief 设置服务运行用户
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg SetServiceUser(const std::string &serviceName);

        /**
         * @brief 生成并创建 systemd service 文件
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg GenerateAndCreateServiceFile(const std::string &serviceName);

        /**
         * @brief 收集服务运行时信息
         * @param serviceName 服务名称
         * @return ServiceRuntimeInfo 运行时信息
         */
        ServiceRuntimeInfo CollectRuntimeInfo(const std::string &serviceName);

        /**
         * @brief 将 systemd ActiveState 转换为内部 ServiceStatus
         * @param activeState systemd 状态字符串
         * @return ServiceStatus 转换后的状态
         */
        ServiceStatus ConvertActiveStateToStatus(const std::string &activeState);

        /**
         * @brief 清理临时目录
         * @param dirPath 目录路径
         */
        void CleanupTempDirectory(const std::string &dirPath);

        /**
         * @brief 初始化文件管理器
         * @return ResultMsg 操作结果
         */
        ResultMsg InitializeFileManager();

        /**
         * @brief 初始化 DBus 管理器
         * @return ResultMsg 操作结果
         */
        ResultMsg InitializeDBusManager();

        /**
         * @brief 将服务名转换为 systemd 单元名（添加 scmd_ 前缀）
         * @param serviceName 服务名称
         * @return std::string systemd 单元名
         */
        static std::string ToSystemdUnitName(const std::string &serviceName);

        /**
         * @brief 解析服务名（从解压目录的 service.yaml 中读取）
         * @param serviceName 传入的服务名称
         * @param extractDir 解压目录路径
         * @return std::string 解析后的服务名称，失败返回空字符串
         */
        std::string ResolveServiceName(const std::string &serviceName, const std::string &extractDir);

        /**
         * @brief 标记启动序列需要重新计算
         */
        void MarkSequenceDirty();

        /**
         * @brief 确保启动序列是最新的（如果 dirty 则重新计算）
         * @return ResultMsg 操作结果
         */
        ResultMsg EnsureSequenceUpdated();

        /**
         * @brief 获取依赖指定服务的所有服务（反向依赖）
         * @param serviceName 服务名称
         * @return std::vector<std::string> 依赖此服务的服务列表
         */
        std::vector<std::string> GetDependentServices(const std::string &serviceName);

    private:
        std::shared_ptr<ConfigLoader> mConfigLoader;
        std::unique_ptr<FileManager> mFileManager;
        std::unique_ptr<DBusManager> mDBusManager;
        ServiceSequence mServiceSequence;  // 缓存的启动/停止序列
        bool mInitialized;
        bool mSequenceDirty;  // 序列是否需要重新计算
    };
}  // namespace qifeng::scm
