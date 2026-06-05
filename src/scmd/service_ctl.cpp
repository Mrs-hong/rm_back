/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/scmd_types.h"
#include "scmd/service_ctl.h"

#include "common/config.h"
#include "common/utils.h"
#include "dbinit/database_init.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/service_manager.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace {
    constexpr const char* DataRootPassword = "123";  // 数据库初始化时管理员密码

}  // namespace
namespace qifeng::scm {

    ResultMsg ServiceControl::Init() {
        mConfigLoader = std::make_shared<ConfigLoader>();
        auto result = mConfigLoader->Initialize();
        if (!result.IsDefalutSuccess() && result.code != 1) {
            return MakeError("Failed to initialize ConfigLoader: " + result.msg);
        }

        const auto &configInfo = mConfigLoader->GetConfigInfo();
        auto logFileSizeBytes = static_cast<size_t>(configInfo.logFileSizeMB) * 1024U * 1024U;
        Logger::GetInstance().Initialize(configInfo.logsDir, "scmd", logFileSizeBytes, configInfo.logFileCount);
        SLOG_INFO << "ServiceControl initializing...";

        mServiceManager = std::make_shared<ServiceManager>(mConfigLoader);

        auto allServices = mConfigLoader->GetAllServices();
        if (allServices.empty()) {
            SLOG_INFO << "No installed services found, skip auto-start";
        } else {
            SLOG_INFO << "Found " << allServices.size() << " installed service(s), starting auto-start services...";
            result = mServiceManager->StartAllAutoStartServices();
            if (!result.IsDefalutSuccess()) {
                SLOG_WARN << "Some auto-start services failed: " << result.msg;
            }
        }

        mIsInit = true;
        SLOG_INFO << "ServiceControl initialized successfully";
        return MakeSuccess();
    }

    ResultMsg ServiceControl::Installed(const std::string &serviceName, const std::string &serviceTarPath) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Installing service: " << serviceName << " from " << serviceTarPath;
        auto result = mServiceManager->InstallService(serviceTarPath, serviceName);
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to install service: " << serviceName << " from " << serviceTarPath
                       << ", error: " << result.msg;
            return result;
        }
        // 若安装的是数据库则初始化数据库表
        std::string actualServiceName = result.msg;
        result = InitDataBase(actualServiceName);
        if (!result.IsDefalutSuccess()) {
            // 数据建库操作失败，回滚安装
            mServiceManager->UninstallService(actualServiceName);
            result = MakeError("install service " + actualServiceName + " failed:" + result.msg);
            SLOG_ERROR << result.msg;
        }
        return result;
    }

    ResultMsg ServiceControl::UninstallService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Uninstalling service: " << serviceName;
        return mServiceManager->UninstallService(serviceName);
    }

    ResultMsg ServiceControl::UpgradeService(const std::string &serviceName, const std::string &serviceTarPath) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Upgrading service: " << serviceName << " from " << serviceTarPath;
        return mServiceManager->UpdateService(serviceName, serviceTarPath);
    }

    ResultMsg ServiceControl::StartService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Starting service: " << serviceName;
        return mServiceManager->StartService(serviceName);
    }

    ResultMsg ServiceControl::StopService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Stopping service: " << serviceName;
        return mServiceManager->StopService(serviceName);
    }

    ResultMsg ServiceControl::RestartService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Restarting service: " << serviceName;
        return mServiceManager->RestartService(serviceName);
    }

    ResultMsg ServiceControl::ReloadService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Reloading service: " << serviceName;
        return mServiceManager->ReloadService(serviceName);
    }

    ResultMsg ServiceControl::GetServiceStatus(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        return mServiceManager->GetServiceStatus(serviceName);
    }

    ServiceRuntimeInfo ServiceControl::GetServiceRuntimeInfo(const std::string &serviceName) {
        if (!mIsInit) {
            return ServiceRuntimeInfo();
        }
        return mServiceManager->GetServiceRuntimeInfo(serviceName);
    }

    ResultMsg ServiceControl::EnableAutoStart(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        return mServiceManager->EnableAutoStart(serviceName);
    }

    ResultMsg ServiceControl::DisableAutoStart(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        return mServiceManager->DisableAutoStart(serviceName);
    }

    DatabaseConfig ServiceControl::BuildDatabaseConfig(const std::string &dbServiceName, bool isInit) {
        DatabaseConfig cfg;

        auto* svc = mConfigLoader->GetServiceByName(dbServiceName);
        if (!svc) {
            SLOG_ERROR << "Service not found: " << dbServiceName;
            return cfg;
        }

        cfg.dbInfo = svc->dbInfo;
        cfg.binDir = mConfigLoader->GetServiceRootDir(dbServiceName);

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
        cfg.adminPwd = isInit ? "" : DataRootPassword;
        cfg.osUser = svc->execInfo.user;
        cfg.osGroup = svc->execInfo.user;

        // 若服务配置未指定运行用户，则使用当前系统用户，确保数据目录权限与当前用户一致
        if (cfg.osUser.empty()) {
            auto currentUser = utils::GetCurrentUserName();
            if (!currentUser.empty()) {
                cfg.osUser = currentUser;
                cfg.osGroup = currentUser;
            }
        }

        return cfg;
    }

    ResultMsg ServiceControl::InitDataBaseService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }

        SLOG_INFO << "Initializing database for service: " << serviceName;

        auto cfg = BuildDatabaseConfig(serviceName, true);
        if (cfg.binDir.empty()) {
            return MakeError("Failed to build database config for service: " + serviceName);
        }
        {  // 初始化数据库、并设置管理员密码
            DatabaseInit dbInit(cfg);

            auto result = dbInit.Initialize();
            if (!result.IsDefalutSuccess()) {
                SLOG_WARN << "Database initialize result: " << result.msg;
                return MakeError("Database initialization failed: " + result.msg);
            }

            result = dbInit.Start();
            if (!result.IsDefalutSuccess()) {
                SLOG_WARN << "Database start result: " << result.msg;
                return MakeError("Failed to start database for init scripts: " + result.msg);
            }

            // 初始化后 root 无密码，先设置密码再执行脚本，避免在 SQL 文件中暴露密码
            std::string setPwdSql = "ALTER USER 'root'@'localhost' IDENTIFIED BY '" + std::string(DataRootPassword) +
                                    "'; FLUSH PRIVILEGES;";
            result = dbInit.ExecuteSQL(setPwdSql);
            if (!result.IsDefalutSuccess()) {
                SLOG_WARN << "Set root password result: " << result.msg;
                dbInit.Stop();
                return MakeError("Failed to set root password: " + result.msg);
            }
            dbInit.SetAdminPassword(DataRootPassword);
            dbInit.Stop();
        }

        SLOG_INFO << "Database initialized successfully for service: " << serviceName;
        return MakeSuccess();
    }

    ResultMsg ServiceControl::CreateDatabaseUser(const std::string &dbServiceName, const std::string &username,
                                                 const std::string &password) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }

        auto cfg = BuildDatabaseConfig(dbServiceName);
        if (cfg.binDir.empty()) {
            return MakeError("Failed to build database: " + dbServiceName);
        }

        cfg.adminPwd = DataRootPassword;
        DatabaseInit dbInit(cfg);
        return dbInit.CreateUser(username, password);
    }

    ResultMsg ServiceControl::CreateDatabase(const std::string &dbServiceName, const std::string &dbName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }

        auto cfg = BuildDatabaseConfig(dbServiceName);
        if (cfg.binDir.empty()) {
            return MakeError("Failed to build database: " + dbServiceName);
        }

        cfg.adminPwd = DataRootPassword;
        DatabaseInit dbInit(cfg);
        return dbInit.CreateDatabase(dbName);
    }

    ResultMsg ServiceControl::CreateDatabaseTable(const std::string &dbServiceName, const std::string &dbName,
                                                  const std::string &tableName, const std::string &columns) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }

        auto cfg = BuildDatabaseConfig(dbServiceName, true);
        if (cfg.binDir.empty()) {
            return MakeError("Failed to build database: " + dbServiceName);
        }

        cfg.adminPwd = DataRootPassword;
        DatabaseInit dbInit(cfg);
        return dbInit.CreateTable(dbName, tableName, columns);
    }

    ResultMsg ServiceControl::ExecuteSQL(const std::string &dbServiceName, const std::string &sql,
                                         const std::string &dbName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }

        auto cfg = BuildDatabaseConfig(dbServiceName);
        if (cfg.binDir.empty()) {
            return MakeError("Failed to build database config for service: " + dbServiceName);
        }

        // InitServiceDataBase 已修改 root 密码，连接时需使用对应凭证
        cfg.adminPwd = DataRootPassword;

        DatabaseInit dbInit(cfg);
        return dbInit.ExecuteSQL(sql, dbName);
    }

    const ConfigLoader &ServiceControl::GetConfigLoader() const {
        return *mConfigLoader;
    }

    ResultMsg ServiceControl::GetAllServicesInfo() {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }

        return MakeSuccess();
    }

    ResultMsg ServiceControl::RestartAllServices() {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }

        SLOG_INFO << "Restarting all services";

        // 先停止所有服务
        auto stopResult = mServiceManager->StopAllServices();
        if (!stopResult.IsDefalutSuccess()) {
            SLOG_WARN << "Some services failed to stop: " << stopResult.msg;
        }

        // 再启动所有autoStart服务
        auto startResult = mServiceManager->StartAllAutoStartServices();
        if (!startResult.IsDefalutSuccess()) {
            SLOG_WARN << "Some services failed to start: " << startResult.msg;
        }

        if (!stopResult.IsDefalutSuccess() || !startResult.IsDefalutSuccess()) {
            return MakeWarning("Some services failed during restart");
        }

        SLOG_INFO << "All services restarted successfully";
        return MakeSuccess();
    }

    ResultMsg ServiceControl::GetOperationLog(int /*logLevel*/, int logCount) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }

        const auto &configInfo = mConfigLoader->GetConfigInfo();
        std::string logPath = configInfo.logsDir + "/scmd.log";

        std::ifstream logFile(logPath);
        if (!logFile.is_open()) {
            return MakeError("Log file not found: " + logPath);
        }

        // 读取所有行，返回最后logCount行内容
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(logFile, line)) {
            lines.push_back(line);
        }
        logFile.close();

        int startIdx =
            logCount > 0 && static_cast<int>(lines.size()) > logCount ? static_cast<int>(lines.size()) - logCount : 0;

        // 将日志内容拼接到msg中
        std::string logContent;
        for (int i = startIdx; i < static_cast<int>(lines.size()); ++i) {
            if (i > startIdx) {
                logContent += "\n";
            }
            logContent += lines[static_cast<size_t>(i)];
        }

        ResultMsg result;
        result.code = 0;
        result.msg = logContent.empty() ? "No log entries" : logContent;
        return result;
    }

    ResultMsg ServiceControl::InitDataBase(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        auto svc = mConfigLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }
        if (svc->isDatabaseService) {
            return InitDataBaseService(serviceName);
        }
        if (svc->dbInfo.dbType != DatabaseType::NONE && !svc->dbInfo.sqlDir.empty()) {
            // 找到第一个是该数据库的服务名
            std::string dbServiceName;
            for (auto &[name, _] : svc->dependencies) {
                const auto tempSer = mConfigLoader->GetServiceByName(name);
                if (tempSer && tempSer->isDatabaseService) {
                    dbServiceName = name;
                    break;
                }
            }
            if (dbServiceName.empty()) {
                return MakeError("No database service found for service: " + serviceName);
            }
            // sqlDir 是当前服务的相对路径，基于当前服务目录解析为绝对路径
            std::string absSqlDir = utils::GetAbsolutePath(svc->currentServiceDir + "/" + svc->dbInfo.sqlDir);
            return ExecuteDbInitScripts(dbServiceName, absSqlDir);
        }
        return MakeSuccess();
    }

    ResultMsg ServiceControl::ExecuteDbInitScripts(const std::string &dbServiceName, const std::string &sqlDir) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        auto cfg = BuildDatabaseConfig(dbServiceName);
        if (cfg.binDir.empty()) {
            return MakeError("Failed to build database config for service: " + dbServiceName);
        }
        cfg.dbInfo.sqlDir = sqlDir;
        DatabaseInit dbInit(cfg);

        // 记录数据库是否已在运行，避免影响原有运行状态
        bool wasRunning = dbInit.IsRunning();

        if (!wasRunning) {
            auto startResult = dbInit.Start();
            if (!startResult.IsDefalutSuccess()) {
                return startResult;
            }
        }

        auto initResult = dbInit.ExecuteInitScripts();
        if (!initResult.IsDefalutSuccess()) {
            // 如果是我们启动的数据库，执行失败也要停止以恢复原状态
            if (!wasRunning) {
                dbInit.Stop();
            }
            return initResult;
        }

        // 仅当数据库由本次启动时才停止，保持原有运行状态
        if (!wasRunning) {
            dbInit.Stop();
        }
        return MakeSuccess();
    }
}  // namespace qifeng::scm
