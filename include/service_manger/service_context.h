/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include <memory>

namespace qifeng::scm {
    class ConfigLoader;
    class FileManager;
    class DBusManager;
    class ServiceManager;
    class NginxManager;
    class ModelManager;
    class UpgradeService;
    class DatabaseService;

    /**
     * @brief 共享依赖上下文
     * @details 持有所有领域管理器的 shared_ptr，由 ServiceControl::Init() 构造后填充。
     *          handler 通过此上下文直接访问子服务，无需经过 ServiceControl 门面。
     *
     *          基础依赖（configLoader/fileManager/dbusManager）在 ServiceManager 构造时确定，
     *          领域服务（serviceManager/nginxManager/modelManager/upgradeService/databaseService）
     *          在 ServiceControl::Init() 中按依赖顺序构造后填充。
     * @note 子服务持有 const ServiceContext& 引用，构造时基础依赖已就绪，
     *       领域服务成员在构造后填充（子服务构造期间不访问领域服务成员）。
     */
    struct ServiceContext {
        // 基础共享依赖（由 ServiceManager 构造时确定）
        std::shared_ptr<ConfigLoader> configLoader;
        std::shared_ptr<FileManager> fileManager;
        std::shared_ptr<DBusManager> dbusManager;

        // 领域服务（由 ServiceControl::Init() 构造后填充）
        std::shared_ptr<ServiceManager> serviceManager;
        std::shared_ptr<NginxManager> nginxManager;
        std::shared_ptr<ModelManager> modelManager;
        std::shared_ptr<UpgradeService> upgradeService;
        std::shared_ptr<DatabaseService> databaseService;
    };
}  // namespace qifeng::scm
