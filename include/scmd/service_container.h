/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/scmd_types.h"
#include "common/types.h"
#include "service_manger/service_context.h"

#include <memory>
#include <string>

namespace qifeng::scm {
    class ConfigLoader;
    class ServiceManager;
    class NginxManager;
    class ModelManager;
    class UpgradeService;
    class DatabaseService;

    /**
     * @brief 服务容器：scmd 的子服务初始化器与依赖容器
     * @details 负责创建、初始化所有子服务（ConfigLoader/ServiceManager/NginxManager/ModelManager/UpgradeService/DatabaseService），
     * 通过 GetServiceContext() 暴露给 handler 使用。handler 通过 ServiceContext 直接访问子服务，无需经过此容器。
     */
    class ServiceContainer {
    public:
        /**
         * @brief 初始化服务主类
         * @details 加载配置、初始化日志系统、创建ServiceManager、启动已安装且autoStart的服务
         * @return ResultMsg 操作结果
         */
        ResultMsg Init();

        /**
         * @brief 获取配置加载器
         * @return const ConfigLoader& 配置加载器引用
         */
        const ConfigLoader &GetConfigLoader() const;

        /**
         * @brief 获取服务上下文（含所有子服务）
         * @return const ServiceContext& 服务上下文引用
         */
        const ServiceContext &GetServiceContext() const { return mContext; }

    private:
        std::shared_ptr<ConfigLoader> mConfigLoader;      // 配置加载器
        std::shared_ptr<ServiceManager> mServiceManager;  // 服务管理器
        ServiceContext mContext;                          // 共享依赖上下文（需保活以供领域管理器持有引用）
        std::shared_ptr<NginxManager> mNginxManager;      // Nginx 配置管理器
        std::shared_ptr<ModelManager> mModelManager;      // 模型文件管理器
        // 注意：使用 elaborated type specifier（class UpgradeService）避免与同名成员函数 UpgradeService 冲突
        std::shared_ptr<class UpgradeService> mUpgradeService;  // 升级服务管理器
        std::shared_ptr<DatabaseService> mDatabaseService;      // 数据库服务管理器
    };
}  // namespace qifeng::scm
