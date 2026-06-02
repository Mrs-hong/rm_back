/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/service_ctl.h"

#include "common/config.h"
#include "dbinit/database_init.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/service_manager.h"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace qifeng::scm {

    ResultMsg ServiceMain::Init() {
        mConfigLoader = std::make_shared<ConfigLoader>();
        auto result = mConfigLoader->Initialize();
        if (!result.IsDefalutSuccess() && result.code != 1) {
            return MakeError("Failed to initialize ConfigLoader: " + result.msg);
        }

        const auto &configInfo = mConfigLoader->GetConfigInfo();
        auto logFileSizeBytes = static_cast<size_t>(configInfo.logFileSizeMB) * 1024U * 1024U;
        Logger::GetInstance().Initialize(configInfo.logsDir, "scmd", logFileSizeBytes, configInfo.logFileCount);
        SLOG_INFO << "ServiceMain initializing...";

        mServiceManager = std::make_shared<ServiceManager>(mConfigLoader);

        auto allServices = mConfigLoader->GetAllServices();
        if (allServices.empty()) {
            SLOG_INFO << "No installed services found, skip auto-start";
            std::cout << "[ServiceMain] 首次启动，无已安装服务" << std::endl;
        } else {
            SLOG_INFO << "Found " << allServices.size() << " installed service(s), starting auto-start services...";
            result = mServiceManager->StartAllAutoStartServices();
            if (!result.IsDefalutSuccess()) {
                SLOG_WARN << "Some auto-start services failed: " << result.msg;
            }
        }

        mIsInit = true;
        SLOG_INFO << "ServiceMain initialized successfully";
        return MakeSuccess();
    }

    ResultMsg ServiceMain::Installed(const std::string &serviceTarPath, const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }
        SLOG_INFO << "Installing service: " << serviceName << " from " << serviceTarPath;
        return mServiceManager->InstallService(serviceTarPath, serviceName);
    }

    ResultMsg ServiceMain::UninstallService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }
        SLOG_INFO << "Uninstalling service: " << serviceName;
        return mServiceManager->UninstallService(serviceName);
    }

    ResultMsg ServiceMain::StartService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }
        SLOG_INFO << "Starting service: " << serviceName;
        return mServiceManager->StartService(serviceName);
    }

    ResultMsg ServiceMain::StopService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }
        SLOG_INFO << "Stopping service: " << serviceName;
        return mServiceManager->StopService(serviceName);
    }

    ResultMsg ServiceMain::RestartService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }
        SLOG_INFO << "Restarting service: " << serviceName;
        return mServiceManager->RestartService(serviceName);
    }

    ResultMsg ServiceMain::ReloadService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }
        SLOG_INFO << "Reloading service: " << serviceName;
        return mServiceManager->ReloadService(serviceName);
    }

    ResultMsg ServiceMain::GetServiceStatus(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }
        return mServiceManager->GetServiceStatus(serviceName);
    }

    ResultMsg ServiceMain::EnableAutoStart(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }
        return mServiceManager->EnableAutoStart(serviceName);
    }

    ResultMsg ServiceMain::DisableAutoStart(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }
        return mServiceManager->DisableAutoStart(serviceName);
    }

    DatabaseConfig ServiceMain::BuildDatabaseConfig(const std::string &serviceName) {
        DatabaseConfig cfg;

        auto *svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            SLOG_ERROR << "Service not found: " << serviceName;
            return cfg;
        }

        cfg.dbInfo = svc->dbInfo;
        cfg.binDir = mConfigLoader->GetServiceRootDir(serviceName);

        if (!svc->execInfo.dataDir.empty()) {
            fs::path dataPath(svc->execInfo.dataDir);
            if (dataPath.is_relative()) {
                cfg.dataDir = (fs::path(cfg.binDir) / dataPath).string();
            } else {
                cfg.dataDir = svc->execInfo.dataDir;
            }
        }

        cfg.port = svc->resourcesInfo.ports.empty() ? 0 : svc->resourcesInfo.ports[0];
        cfg.adminUser = "root";
        cfg.adminPwd = "";
        cfg.osUser = svc->execInfo.user;
        cfg.osGroup = svc->execInfo.user;

        return cfg;
    }

    ResultMsg ServiceMain::InitServiceDataBase(const std::string &serviceName, const std::string &sqlDir) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }

        SLOG_INFO << "Initializing database for service: " << serviceName;

        auto cfg = BuildDatabaseConfig(serviceName);
        if (cfg.binDir.empty()) {
            return MakeError("Failed to build database config for service: " + serviceName);
        }

        cfg.dbInfo.sqlDir = sqlDir;
        DatabaseInit dbInit(cfg);

        auto result = dbInit.Initialize();
        if (!result.IsDefalutSuccess()) {
            SLOG_WARN << "Database initialize result: " << result.msg;
            if (result.code == -1) {
                return MakeError("Database initialization failed: " + result.msg);
            }
        }

        result = dbInit.Start();
        if (!result.IsDefalutSuccess()) {
            SLOG_WARN << "Database start result: " << result.msg;
            if (result.code == -1) {
                return MakeError("Failed to start database for init scripts: " + result.msg);
            }
        }

        result = dbInit.ExecuteInitScripts();
        if (!result.IsDefalutSuccess()) {
            SLOG_WARN << "Execute init scripts result: " << result.msg;
            if (result.code == -1) {
                dbInit.Stop();
                return MakeError("Failed to execute init scripts: " + result.msg);
            }
        }

        dbInit.Stop();

        SLOG_INFO << "Database initialized successfully for service: " << serviceName;
        return MakeSuccess();
    }

    ResultMsg ServiceMain::CreateDatabaseUser(const std::string &serviceName, const std::string &username,
                                               const std::string &password) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }

        auto cfg = BuildDatabaseConfig(serviceName);
        if (cfg.binDir.empty()) {
            return MakeError("Failed to build database config for service: " + serviceName);
        }

        DatabaseInit dbInit(cfg);
        return dbInit.CreateUser(username, password);
    }

    ResultMsg ServiceMain::CreateServiceDatabase(const std::string &serviceName, const std::string &dbName) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }

        auto cfg = BuildDatabaseConfig(serviceName);
        if (cfg.binDir.empty()) {
            return MakeError("Failed to build database config for service: " + serviceName);
        }

        DatabaseInit dbInit(cfg);
        return dbInit.CreateDatabase(dbName);
    }

    ResultMsg ServiceMain::CreateDatabaseTable(const std::string &serviceName, const std::string &dbName,
                                                const std::string &tableName, const std::string &columns) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }

        auto cfg = BuildDatabaseConfig(serviceName);
        if (cfg.binDir.empty()) {
            return MakeError("Failed to build database config for service: " + serviceName);
        }

        DatabaseInit dbInit(cfg);
        return dbInit.CreateTable(dbName, tableName, columns);
    }

    ResultMsg ServiceMain::ExecuteServiceSQL(const std::string &serviceName, const std::string &sql,
                                              const std::string &dbName) {
        if (!mIsInit) {
            return MakeError("ServiceMain is not initialized");
        }

        auto cfg = BuildDatabaseConfig(serviceName);
        if (cfg.binDir.empty()) {
            return MakeError("Failed to build database config for service: " + serviceName);
        }

        DatabaseInit dbInit(cfg);
        return dbInit.ExecuteSQL(sql, dbName);
    }

}  // namespace qifeng::scm
