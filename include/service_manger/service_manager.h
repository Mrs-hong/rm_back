/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/scmd_types.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include "service_manger/service_context.h"

#include <memory>
#include <string>
#include <vector>

namespace qifeng::scm {
    class ConfigLoader;
    class FileManager;
    class DBusManager;

    /**
     * @brief 服务管理器（生命周期与共享工具集）
     *
     * 重构后职责收敛为：
     *   1. 服务生命周期管理（安装/启动/停止/重启/卸载/自启/状态查询/序列管理）；
     *   2. 跨领域共享的底层工具方法（解包/服务文件生成/运行时信息收集/依赖解析等），
     *      供 NginxManager/ModelManager/UpgradeService 等领域管理器复用；
     *   3. 暴露 GetServiceContext() 供 handler 通过 ServiceContext 直接访问子服务。
     *
     * 已迁出的职责：
     *   - 数据库相关 → DatabaseService
     *   - Nginx 配置 → NginxManager
     *   - 模型文件   → ModelManager
     *   - 升级编排   → UpgradeService（含 UpgradeArtifacts 定义）
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

        // === 生命周期管理 ===

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
         * @brief 启动 scmd 自身（qifeng-scmd.service）
         * @details scmd 正在处理请求即说明自身已在运行，直接返回成功
         * @return ResultMsg 操作结果
         */
        ResultMsg StartScmdSelf();

        /**
         * @brief 停止 scmd 自身（qifeng-scmd.service）
         * @details 通过 systemd DBus 停止自身守护进程
         * @return ResultMsg 操作结果
         */
        ResultMsg StopScmdSelf();

        /**
         * @brief 重启 scmd 自身（qifeng-scmd.service）
         * @details 通过 systemd DBus 重启自身守护进程
         * @return ResultMsg 操作结果
         */
        ResultMsg RestartScmdSelf();

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
         * @brief 判断服务是否处于活跃（运行中）状态
         * @details 仅通过 DBus 查询 systemd ActiveState，轻量级，不收集完整运行时信息
         * @param serviceName 服务名称
         * @return bool true 表示服务处于 active 状态
         */
        bool IsServiceActive(const std::string &serviceName);

        /**
         * @brief 卸载服务
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg UninstallService(const std::string &serviceName);

        /**
         * @brief 清除服务名可能存在的数据
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg ClearServiceData(const std::string &serviceName);

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
         * @brief 重新生成所有已注册服务的 systemd service 文件
         * @details 用于 scmd 启动时将新增的 systemd 配置（如 StandardOutput、LimitCORE）
         *          应用到已安装服务，保证升级兼容性。单个服务失败不影响其它服务。
         * @return ResultMsg 操作结果（部分失败时返回警告，包含失败服务名列表）
         */
        ResultMsg RegenerateAllServiceFiles();

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

        // === 共享底层工具方法（供领域管理器复用） ===

        /**
         * @brief 解压软件tar包到指定目录并可选指定解压后的文件名
         * @param tarPath tar包路径
         * @param extractDir 输出参数，解压目录路径
         * @param newName 可选，指定解压后的目录名，默认为空字符串表示不处理
         * @return ResultMsg 操作结果
         */
        ResultMsg ExtractSoftwareTar(const std::string &tarPath, const std::string &extractDir,
                                     const std::string &newName = "");

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
         * @brief 解析服务名（从指定 service.yaml 路径读取）
         * @param yamlPath service.yaml 完整路径
         * @return std::string 解析后的服务名称，失败返回空字符串
         */
        std::string ResolveServiceName(const std::string &yamlPath);

        /**
         * @brief 标记启动序列需要重新计算
         */
        void MarkSequenceDirty();

        /**
         * @brief 获取依赖指定服务的所有服务（反向依赖）
         * @param serviceName 服务名称
         * @return std::vector<std::string> 依赖此服务的服务列表
         */
        std::vector<std::string> GetDependentServices(const std::string &serviceName);

        /**
         * @brief 判断升级输入是 tar 包还是已解压目录
         * @param path 输入路径
         * @return bool true 表示是 .tar.gz 包，false 表示目录
         */
        bool IsTarPackage(const std::string &path) const;

        /**
         * @brief 将服务名转换为 systemd 单元名（添加 scmd_ 前缀）
         * @param serviceName 服务名称
         * @return std::string systemd 单元名
         */
        static std::string ToSystemdUnitName(const std::string &serviceName);

        // === 上下文访问 ===

        /**
         * @brief 获取共享依赖上下文（基础依赖部分）
         * @details 返回包含 configLoader/fileManager/dbusManager 的部分上下文，
         *          调用方（ServiceControl::Init）负责填充领域服务成员。
         *          fileManager 和 dbusManager 以 shared_ptr 共享所有权，
         *          保证在 ServiceManager 之外的生命周期内有效。
         * @return ServiceContext 共享依赖上下文（领域服务成员为空，由调用方填充）
         */
        ServiceContext GetServiceContext() const;

    private:
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
         * @brief 设置服务运行用户
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg SetServiceUser(const std::string &serviceName);

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
         * @brief 确保启动序列是最新的（如果 dirty 则重新计算）
         * @return ResultMsg 操作结果
         */
        ResultMsg EnsureSequenceUpdated();

    private:
        std::shared_ptr<ConfigLoader> mConfigLoader;
        // 使用 shared_ptr 以便通过 GetServiceContext() 共享给领域管理器
        std::shared_ptr<FileManager> mFileManager;
        std::shared_ptr<DBusManager> mDBusManager;
        ServiceSequence mServiceSequence;  // 缓存的启动/停止序列
        bool mInitialized;
        bool mSequenceDirty;  // 序列是否需要重新计算
    };
}  // namespace qifeng::scm
