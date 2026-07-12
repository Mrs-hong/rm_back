/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/scmd_types.h"
#include "common/types.h"
#include "scmd/service_container.h"

#include "common/config.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/database_service.h"
#include "service_manger/model_manager.h"
#include "service_manger/nginx_manager.h"
#include "service_manger/service_manager.h"
#include "service_manger/upgrade_service.h"

namespace qifeng::scm {

    ResultMsg ServiceContainer::Init() {
        mConfigLoader = std::make_shared<ConfigLoader>();
        auto result = mConfigLoader->Initialize();
        if (!result.IsDefaultSuccess() && result.code != 1) {
            return MakeError("Failed to initialize ConfigLoader: " + result.msg);
        }

        const auto &configInfo = mConfigLoader->GetConfigInfo();
        auto logFileSizeBytes = static_cast<size_t>(configInfo.logFileSizeMB) * 1024U * 1024U;
        Logger::GetInstance().Initialize(configInfo.logsDir, "scmd.log", logFileSizeBytes, configInfo.logFileCount);
        SLOG_INFO << "ServiceContainer initializing...";

        mServiceManager = std::make_shared<ServiceManager>(mConfigLoader);

        // 构造共享依赖上下文和领域管理器
        // 注意：mContext 需作为成员保活，因为 NginxManager/ModelManager/UpgradeService/DatabaseService
        //       内部以 const ServiceContext& 引用持有它
        mContext = mServiceManager->GetServiceContext();
        // 填充领域服务到 mContext，使 handler 可通过 ServiceContext 直接访问
        mContext.serviceManager = mServiceManager;
        mDatabaseService = std::make_shared<DatabaseService>(mContext);
        mContext.databaseService = mDatabaseService;
        mNginxManager = std::make_shared<NginxManager>(mContext, *mServiceManager);
        mContext.nginxManager = mNginxManager;
        mModelManager = std::make_shared<ModelManager>(mContext, *mServiceManager);
        mContext.modelManager = mModelManager;
        mUpgradeService = std::make_shared<class UpgradeService>(mContext, *mServiceManager, *mModelManager,
                                                                  *mNginxManager, *mDatabaseService);
        mContext.upgradeService = mUpgradeService;

        auto allServices = mConfigLoader->GetAllServices();
        if (allServices.empty()) {
            SLOG_INFO << "No installed services found, skip auto-start";
        } else {
            SLOG_INFO << "Found " << allServices.size() << " installed service(s), starting auto-start services...";
            result = mServiceManager->StartAllAutoStartServices();
            if (!result.IsDefaultSuccess()) {
                SLOG_WARN << "Some auto-start services failed: " << result.msg;
            }
        }

        SLOG_INFO << "ServiceContainer initialized successfully";
        return MakeSuccess();
    }

    const ConfigLoader &ServiceContainer::GetConfigLoader() const {
        return *mConfigLoader;
    }

}  // namespace qifeng::scm
