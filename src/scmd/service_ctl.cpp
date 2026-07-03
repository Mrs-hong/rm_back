/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/scmd_types.h"
#include "common/types.h"
#include "scmd/service_ctl.h"

#include "common/config.h"
#include "common/utils.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/file_manager.h"
#include "service_manger/service_manager.h"
#include "service_tool/tool_mariadb.h"
#include "service_tool/tools_def.h"

#include <algorithm>
#include <fstream>
#include <systemd/sd-journal.h>

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

        std::string actualServiceName = result.msg;
        result = InitServiceDatabase(actualServiceName);
        if (!result.IsDefalutSuccess()) {
            // 数据建库操作失败，回滚安装
            UninstallService(actualServiceName);
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
        auto svc = mConfigLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }
        DatabaseType dbType = svc->dbInfo.dbType;
        ResultMsg ret = mServiceManager->UninstallService(serviceName);
        // 清除数据库相关
        if (dbType != DatabaseType::NONE) {
            ResultMsg result = ClearDatabaseData(dbType, serviceName);
            if (ret.code == -1) {
                return MakeError("uninstall service failed:" + ret.msg);
            }
            if (result.code == -1) {
                return MakeError("clear database data failed:" + result.msg);
            }
        }
        return ret;
    }

    ResultMsg ServiceControl::UpgradeService(const std::string &serviceName, const std::string &serviceTarPath) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Upgrading service: " << serviceName << " from " << serviceTarPath;
        auto result = mServiceManager->UpdateService(serviceName, serviceTarPath);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        // 确认升级完成，清理旧版本备份
        auto cleanResult = mServiceManager->CleanUpgradeBackup(serviceName);
        if (!cleanResult.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to clean upgrade backup: " << cleanResult.msg;
        }

        return MakeSuccess();
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
            return ServiceRuntimeInfo {};
        }
        return mServiceManager->GetServiceRuntimeInfo(serviceName);
    }

    bool ServiceControl::IsServiceActive(const std::string &serviceName) {
        if (!mIsInit) {
            return false;
        }
        return mServiceManager->IsServiceActive(serviceName);
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

    ResultMsg ServiceControl::InitServiceDatabase(const std::string &serviceName) {
        auto svc = mConfigLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }

        if (svc->dbInfo.dbType != DatabaseType::NONE && !svc->dbInfo.sqlDir.empty()) {
            SLOG_INFO << "Creating database user for service: " << serviceName;
            std::string dbUserName = serviceName;
            std::string dbPassword = utils::GenerateRandomPassword(12);
            SLOG_DEBUG << "start create " << serviceName << " db user password file ";
            ResultMsg res = CreateDbUserPassword(*svc, dbUserName, dbPassword);
            if (!res.IsDefalutSuccess()) {
                SLOG_WARN << "Write db user password error, do clear  ";
                ClearDbUserPasswordFile(*svc);
                return res;
            }
            res = CreateDatabaseUser(svc->dbInfo.dbType, dbUserName, dbPassword);
            if (!res.IsDefalutSuccess()) {
                SLOG_WARN << "Create database user error, do clear  ";
                ClearDbUserPasswordFile(*svc);
                return res;
            }

            SLOG_INFO << "Executing database init scripts for service: " << serviceName;
            // sqlDir 是当前服务的相对路径，基于当前服务目录解析为绝对路径
            std::string absSqlDir = utils::GetAbsolutePath(utils::JoinPath(svc->currentServiceDir, svc->dbInfo.sqlDir));
            res = ExecuteDbInitScripts(svc->dbInfo.dbType, absSqlDir, dbUserName, dbPassword);
            if (!res.IsDefalutSuccess()) {
                // 执行脚本失败，删除用户
                SLOG_WARN << "Failed to execute database init scripts for service: " << serviceName
                          << ", deleting user: " << dbUserName;
                DeleteDatabaseUser(svc->dbInfo.dbType, dbUserName);
                ClearDbUserPasswordFile(*svc);
            }
            return res;
        }
        return MakeSuccess();
    }

    std::string ServiceControl::GetDependentDatabaseServiceName(const std::string &serviceName) {
        auto svc = mConfigLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return "";
        }
        std::string dbServiceName;
        for (auto &[name, _] : svc->dependencies) {
            if (tool::IsMariadbService(name)) {
                dbServiceName = name;
                break;
            }
        }
        return dbServiceName;
    }

    ResultMsg ServiceControl::CreateDbUserPassword(const ServiceDefinition &serviceDefinition,
                                                   const std::string &dbUserName, const std::string &dbPassword) {
        std::string actualFilePath = utils::JoinPath(serviceDefinition.currentServiceDir, serviceDefinition.dbInfo.outputDir);
        auto res = utils::CreateDirectory(actualFilePath);
        if (!res.IsDefalutSuccess()) {
            return MakeError("Write db user password error when create directory failed: " + actualFilePath);
        }
        std::string dbPassFile = utils::JoinPath(actualFilePath, serviceDefinition.serviceName);
        std::ofstream ofs(dbPassFile, std::ios::trunc);
        if (!ofs.is_open()) {
            return MakeError("Write db user password error when failed to open file: " + dbPassFile);
        }
        ofs << dbUserName << std::endl << dbPassword << std::endl;
        return MakeSuccess();
    }

    ResultMsg ServiceControl::ClearDbUserPasswordFile(const ServiceDefinition &serviceDefinition) {
        std::string actualFilePath = utils::JoinPath(serviceDefinition.currentServiceDir, serviceDefinition.dbInfo.outputDir);
        std::string dbPassFile = utils::JoinPath(actualFilePath, serviceDefinition.serviceName);
        auto res = utils::RemoveFile(dbPassFile);
        if (!res.IsDefalutSuccess()) {
            return MakeError("Clear db user password file error when remove file failed: " + dbPassFile);
        }
        return MakeSuccess();
    }

    ResultMsg ServiceControl::CreateDatabaseUser(const DatabaseType &dbType, const std::string &username,
                                                 const std::string &password) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        ResultMsg result;
        if (dbType == DatabaseType::MYSQL) {
            tool::MariadbDef dbDef;
            tool::Mariadb mariadb(tool::MariadbDef {});
            return mariadb.CreateUser(username, password);
        }
        return MakeError("Unsupported database service ");
    }

    ResultMsg ServiceControl::DeleteDatabaseUser(const DatabaseType &dbType, const std::string &username) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        ResultMsg result;
        if (dbType == DatabaseType::MYSQL) {
            tool::Mariadb mariadb(tool::MariadbDef {});
            return mariadb.DeleteUserAndDatabase(username);
        }
        return MakeError("Unsupported database service ");
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
        std::string logPath = utils::JoinPath(configInfo.logsDir, "scmd.log");

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

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceControl::GetServiceJournal(const std::string &serviceName, int logCount) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        if (serviceName.empty()) {
            return MakeError("Service name is empty");
        }
        // 校验服务已注册，避免查询任意系统服务
        if (mConfigLoader->GetServiceByName(serviceName) == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }

        // 行数边界处理：<=0 时使用默认 10
        int count = logCount > 0 ? logCount : 10;

        // 构造 systemd 单元名（scmd_ + serviceName），与 ServiceManager::ToSystemdUnitName 规则一致
        std::string unitName = std::string(FileManager::GetServiceFilePrefix()) + serviceName;

        // sd-journal 句柄使用 RAII 确保释放
        sd_journal *journal = nullptr;
        auto cleanup = [&journal]() {
            if (journal != nullptr) {
                sd_journal_close(journal);
                journal = nullptr;
            }
        };

        // 打开本地 journal
        int r = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY);
        if (r < 0) {
            SLOG_ERROR << "Failed to open journal: " << strerror(-r);
            return MakeError("Failed to open journal: " + std::string(strerror(-r)));
        }

        // 添加单元过滤条件：_SYSTEMD_UNIT=<unit>.service
        std::string match = "_SYSTEMD_UNIT=" + unitName + ".service";
        r = sd_journal_add_match(journal, match.c_str(), 0);
        if (r < 0) {
            SLOG_ERROR << "Failed to add journal match: " << strerror(-r);
            cleanup();
            return MakeError("Failed to add journal match: " + std::string(strerror(-r)));
        }

        // 跳到日志末尾，向前读取最近 count 条
        r = sd_journal_seek_tail(journal);
        if (r < 0) {
            SLOG_ERROR << "Failed to seek journal tail: " << strerror(-r);
            cleanup();
            return MakeError("Failed to seek journal tail: " + std::string(strerror(-r)));
        }

        // entries 按从新到旧收集，最后反转为从旧到新（与 journalctl -n 输出顺序一致）
        std::vector<std::string> entries;
        entries.reserve(static_cast<size_t>(count));
        static constexpr const char* MessagePrefix = "MESSAGE=";
        static const size_t MessagePrefixLen = strlen(MessagePrefix);

        while (static_cast<int>(entries.size()) < count) {
            r = sd_journal_previous(journal);
            if (r == 0) {
                break;  // 到达日志开头
            }
            if (r < 0) {
                SLOG_ERROR << "Failed to iterate journal: " << strerror(-r);
                cleanup();
                return MakeError("Failed to iterate journal: " + std::string(strerror(-r)));
            }

            // 读取 MESSAGE 字段（格式为 "MESSAGE=<内容>"）
            const void *data = nullptr;
            size_t length = 0;
            r = sd_journal_get_data(journal, "MESSAGE", &data, &length);
            if (r < 0) {
                continue;  // 无 MESSAGE 字段，跳过该条
            }
            // 去掉 "MESSAGE=" 前缀，提取正文
            std::string line(static_cast<const char *>(data) + MessagePrefixLen, length - MessagePrefixLen);
            entries.push_back(std::move(line));
        }

        cleanup();

        // 反转为从旧到新，逐行拼接（每条一行，末尾保留换行）
        std::reverse(entries.begin(), entries.end());
        std::string output;
        for (const auto &entry : entries) {
            output += entry + "\n";
        }

        ResultMsg result;
        result.code = 0;
        result.msg = output;
        return result;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    ResultMsg ServiceControl::ExecuteDbInitScripts(const DatabaseType &dbType, const std::string &sqlDir,
                                                   const std::string &dbUserName, const std::string &dbPassword) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        if (dbType == DatabaseType::MYSQL) {
            tool::Mariadb mariadb(tool::MariadbDef {});
            // 获得sqlDir下全部以.sql结尾的脚本绝对路径
            std::vector<std::string> sqlFiles;
            ResultMsg res = utils::GetAllFilesInDir(sqlFiles, sqlDir, ".sql");
            if (!res.IsDefalutSuccess()) {
                return MakeError("Get all sql files error: " + res.msg);
            }
            std::sort(sqlFiles.begin(), sqlFiles.end());
            for (const auto &sqlFile : sqlFiles) {
                SLOG_INFO << "Executing SQL file: " << sqlFile;
                res = mariadb.ExecuteSqlFileByUser(dbUserName, dbPassword, sqlFile);
                if (!res.IsDefalutSuccess()) {
                    return MakeError("Execute SQL file: " + sqlFile + " error: " + res.msg);
                }
            }
        } else {
            return MakeError("Unsupported database service");
        }
        return MakeSuccess();
    }

    ResultMsg ServiceControl::ClearServiceData(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        auto svc = mConfigLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }
        mServiceManager->ClearServiceData(serviceName);
        // 清除数据库相关
        if (svc->dbInfo.dbType != DatabaseType::NONE) {
            ResultMsg result = ClearDatabaseData(svc->dbInfo.dbType, serviceName);
            if (result.code == -1) {
                return MakeError("clear database data failed:" + result.msg);
            }
        }
        return MakeSuccess();
    }

    ResultMsg ServiceControl::ClearDatabaseData(const DatabaseType &dbType, const std::string &serviceName) {
        return DeleteDatabaseUser(dbType, serviceName);
    }

}  // namespace qifeng::scm
