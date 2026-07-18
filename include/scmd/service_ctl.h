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

namespace qifeng::scm {
    class ConfigLoader;
    class ServiceManager;
    class DatabaseService;
    class NginxManager;
    class ModelManager;
    class UpgradeService;

    /**
     * @brief 服务主类：scmd 的核心功能入口（重构后为薄容器）
     * @details 重构后职责收敛为：
     *   1. Init() 构造并装配所有领域管理器（ServiceManager/DatabaseService/NginxManager/
     *      ModelManager/UpgradeService）到 ServiceContext；
     *   2. 暴露 GetServiceContext() 供 handler 通过 ServiceContext 直接访问子服务；
     *   3. 保留少量不便下沉的查询/编排方法作为薄包装。
     *
     * 已迁出的职责（handler 通过 ServiceContext 直接调用领域管理器）：
     *   - 服务生命周期（安装/启动/停止/重启/卸载/自启） → ServiceManager
     *   - 数据库操作（用户管理/初始化/清理/升级备份恢复） → DatabaseService
     *   - Nginx 配置 → NginxManager
     *   - 模型文件 → ModelManager
     *   - 升级编排（含一体化升级） → UpgradeService
     */
    class ServiceControl {
    public:
        // === 初始化与上下文 ===

        /**
         * @brief 初始化服务主类
         * @details 加载配置、初始化日志系统、预创建日志目录、创建 ServiceManager、
         *          构造 ServiceContext 与 4 个领域管理器（DB → Nginx → Model → Upgrade）、
         *          重新生成已安装服务的 systemd 文件、启动 autoStart 服务。
         * @return ResultMsg 操作结果
         */
        ResultMsg Init();

        /**
         * @brief 获取配置加载器
         * @return const ConfigLoader& 配置加载器引用
         */
        const ConfigLoader &GetConfigLoader() const;

        /**
         * @brief 获取共享依赖上下文（供 handler/ScmServer 直接访问子服务）
         * @return const ServiceContext& 共享依赖上下文引用
         */
        const ServiceContext &GetServiceContext() const;

        // === 扩展接口（供 ScmServer 调用，不便下沉到领域管理器） ===

        /**
         * @brief 获取所有服务信息（用于 LIST 命令）
         * @return ResultMsg 操作结果，成功时 msg 包含服务列表 JSON
         */
        ResultMsg GetAllServicesInfo();

        /**
         * @brief 重启所有服务（用于 RESTART_ALL 命令）
         * @details 按依赖逆序停止所有运行中服务，再按依赖顺序启动
         * @return ResultMsg 操作结果
         */
        ResultMsg RestartAllServices();

        /**
         * @brief 获取操作日志（用于 LOG 命令）
         * @param logLevel 日志级别过滤
         * @param logCount 日志条数限制
         * @return ResultMsg 操作结果，成功时 data 包含日志内容
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

        /**
         * @brief 获取服务日志（新需求3.1：优先读日志文件，回退 journal）
         * @details 读取顺序：
         *   1. 优先读取服务日志文件 <logsDir>/<serviceName>/<serviceName>.log 的最后 logCount 行
         *   2. 文件不存在或为空时，回退读取 systemd journal
         * @param serviceName 服务名称（空表示 scmd 自身，读取 <logsDir>/qifeng-scm/qifeng-scm.log，
         *                     journal 回退时使用 unit "qifeng-scmd"）
         * @param logCount 日志行数（<=0 时使用默认 10）
         * @return ResultMsg 成功时 msg 为日志内容
         */
        ResultMsg GetServiceLog(const std::string &serviceName, int logCount);

    private:
        /**
         * @brief 预创建服务日志目录（新需求3.1）
         * @details 创建 <logsDir>/qifeng-scm/ 和每个已注册服务的 <logsDir>/<serviceName>/ 目录，
         *          确保 systemd StandardOutput=append: 能正常写入。systemd 不会自动创建父目录。
         */
        void CreateServiceLogDirs();

        /**
         * @brief 将服务最近 journal 记录同步追加到服务日志文件（新需求3.1）
         * @details 在 Start/Stop/Restart 后调用，将 systemd 自身的操作记录
         *          （如 "Started xxx"、"Stopped xxx"）追加到 <logsDir>/<serviceName>/<serviceName>.log，
         *          与 StandardOutput 重定向的服务进程输出汇聚在同一文件。
         * @param serviceName 服务名称（空或未注册时直接返回）
         */
        void SyncJournalToServiceLog(const std::string &serviceName);

    private:
        bool mIsInit {false};                             // 是否初始化
        std::shared_ptr<ConfigLoader> mConfigLoader;      // 配置加载器
        std::shared_ptr<ServiceManager> mServiceManager;  // 服务管理器（生命周期+共享工具）
        ServiceContext mContext;                          // 共享依赖上下文（持有所有领域管理器）
        std::shared_ptr<DatabaseService> mDatabaseService;  // 数据库服务
        std::shared_ptr<NginxManager> mNginxManager;        // Nginx 配置管理器
        std::shared_ptr<ModelManager> mModelManager;        // 模型文件管理器
        std::shared_ptr<UpgradeService> mUpgradeService;    // 升级服务管理器
    };
}  // namespace qifeng::scm
