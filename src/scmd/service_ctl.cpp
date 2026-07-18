/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/scmd_types.h"
#include "common/types.h"
#include "scmd/service_ctl.h"

#include "common/config.h"
#include "common/utils.h"
#include "common/utils/journal.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/file_manager.h"
#include "service_manger/service_manager.h"
#include "service_tool/tool_mariadb.h"
#include "service_tool/tools_def.h"

#include <algorithm>
#include <filesystem>
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
        Logger::GetInstance().Initialize(configInfo.logsDir, "scmd.log", logFileSizeBytes, configInfo.logFileCount);
        SLOG_INFO << "ServiceControl initializing...";

        // 新需求3.1：预创建日志目录，确保 systemd StandardOutput=append: 能写入
        // - qifeng-scm/：scmd 自身的 systemd 日志（与 scmd.log 隔离）
        // - <serviceName>/：每个服务的 stdout/stderr 及 systemd 操作记录
        CreateServiceLogDirs();

        mServiceManager = std::make_shared<ServiceManager>(mConfigLoader);

        // 升级兼容：启动时重新生成已安装服务的 .service 文件，
        // 应用新增的 StandardOutput/StandardError/SyslogIdentifier/LimitCORE 配置
        auto regenResult = mServiceManager->RegenerateAllServiceFiles();
        if (!regenResult.IsDefalutSuccess()) {
            SLOG_WARN << "Some service files failed to regenerate: " << regenResult.msg;
        }

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
        if (result.code != 0 && result.code != 1) {
            // 安装失败（非警告），直接返回错误
            SLOG_ERROR << "Failed to install service: " << serviceName << " from " << serviceTarPath
                       << ", error: " << result.msg;
            return result;
        }

        // code=0 表示安装成功；code=1 表示安装成功但启动验证失败（警告）
        // 两种情况均需提取纯服务名继续后续处理（数据库初始化等）
        std::string actualServiceName = result.msg;
        std::string verifyWarning;
        if (result.code == 1) {
            // 警告消息格式："<serviceName> installed, but <reason>"
            // 提取服务名（第一个空格前的部分）
            auto spacePos = actualServiceName.find(' ');
            if (spacePos != std::string::npos) {
                verifyWarning = actualServiceName;
                actualServiceName = actualServiceName.substr(0, spacePos);
            }
            SLOG_WARN << "Service installed with verification warning: " << verifyWarning;
        }

        result = InitServiceDatabase(actualServiceName);
        if (!result.IsDefalutSuccess()) {
            // 数据建库操作失败，回滚安装
            UninstallService(actualServiceName);
            return MakeError("install service " + actualServiceName + " failed: " + result.msg);
        }

        // 数据库初始化成功后，若存在验证警告则返回警告（code=1），否则返回成功
        if (!verifyWarning.empty()) {
            return MakeWarning(verifyWarning);
        }
        return MakeSuccess();
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

        // 先清除数据库相关（在删除服务文件之前，以便读取密码文件发现所有数据库）
        if (svc->dbInfo.dbType != DatabaseType::NONE) {
            ResultMsg result = ClearDatabaseData(*svc);
            if (result.code == -1) {
                return MakeError("clear database data failed:" + result.msg);
            }
        }

        // 卸载服务（停止服务 + 删除服务文件 + 从配置中移除）
        ResultMsg ret = mServiceManager->UninstallService(serviceName);
        if (ret.code == -1) {
            return MakeError("uninstall service failed:" + ret.msg);
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

    ResultMsg ServiceControl::PerformInternalUpgrade(const std::string &serviceName) {
        // 向后兼容：转调一体化升级，使用服务内部 soft_dir 作为素材来源
        return PerformIntegratedUpgrade(serviceName, "");
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceControl::PerformIntegratedUpgrade(const std::string &serviceName, const std::string &tarDir) {
        SLOG_INFO << "Perform integrated upgrade for service: " << serviceName
                  << ", tarDir: " << (tarDir.empty() ? "<internal soft_dir>" : tarDir);

        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }

        // 解析升级结果路径（仅在配置了 upgrade.result_path 时记录结果）
        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }
        std::string absResultPath;
        if (!svc->upgradeConfig.resultPath.empty()) {
            absResultPath =
                utils::GetAbsolutePath(utils::JoinPath(svc->currentServiceDir, svc->upgradeConfig.resultPath));
            SLOG_INFO << "Upgrade result will be written to: " << absResultPath;
        } else {
            SLOG_INFO << "No upgrade.result_path configured, skip result file writing";
        }

        std::string upgradeTime = utils::GetCurrentTimeString();
        std::string newVersion;
        std::string modelInstalledName;  // 非空表示已安装模型（需在失败时回退、成功时清理备份）

        // 辅助：写结果文件（失败不影响返回值，仅告警）
        auto writeResult = [&](bool success, const std::string &reason) {
            if (absResultPath.empty()) {
                return;
            }
            auto writeRet = utils::WriteUpgradeResult(absResultPath, success, upgradeTime, newVersion, reason);
            if (!writeRet.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to write upgrade result file: " << writeRet.msg;
            }
        };

        // 辅助：恢复 nginx 正常配置（流程失败时调用）
        auto restoreNginx = [&]() {
            SLOG_INFO << "Restoring nginx to normal mode";
            auto ret = mServiceManager->ResetNginx(NginxResetMode::NORMAL);
            if (!ret.IsDefalutSuccess()) {
                SLOG_ERROR << "Failed to restore nginx: " << ret.msg;
            }
        };

        // 1. 进入 nginx 等待页面（所有路由返回404，避免升级期间访问到不一致状态）
        SLOG_INFO << "Step 1: Set nginx to waiting mode";
        auto waitRet = mServiceManager->ResetNginx(NginxResetMode::WAIT);
        if (!waitRet.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to set nginx waiting mode: " << waitRet.msg;
            // nginx 未进入等待态，直接返回，不继续后续流程
            return waitRet;
        }

        // 1.1 停止服务（若运行中）：升级期间服务必须停止，保证语义一致（升级一定停启）
        //     记录原始运行状态，在所有退出路径上恢复
        bool wasRunning = IsServiceActive(serviceName);
        if (wasRunning) {
            SLOG_INFO << "Step 1.1: Stop running service before upgrade";
            auto stopRet = mServiceManager->StopService(serviceName);
            if (!stopRet.IsDefalutSuccess()) {
                SLOG_ERROR << "Failed to stop service before upgrade: " << stopRet.msg;
                restoreNginx();
                return stopRet;
            }
        } else {
            SLOG_INFO << "Step 1.1: Service not running, skip stop";
        }

        // 辅助：恢复服务运行状态（在所有退出路径上调用，确保升级前运行的服务在结束后恢复运行）
        auto restoreService = [&]() {
            if (wasRunning) {
                SLOG_INFO << "Restoring service to running state: " << serviceName;
                auto startRet = mServiceManager->StartService(serviceName);
                if (!startRet.IsDefalutSuccess()) {
                    SLOG_ERROR << "Failed to restore service running state: " << startRet.msg;
                }
            }
        };

        // 2. 查找升级素材（服务包/model/nginx）
        //    使用 RAII 确保临时解压目录在所有退出路径上被清理
        SLOG_INFO << "Step 2: Find upgrade artifacts";
        UpgradeArtifacts artifacts;
        auto findRet = mServiceManager->FindUpgradeArtifacts(serviceName, tarDir, artifacts);
        if (!findRet.IsDefalutSuccess()) {
            SLOG_ERROR << "Find upgrade artifacts failed: " << findRet.msg;
            restoreNginx();
            restoreService();
            writeResult(false, findRet.msg);
            return findRet;
        }
        // RAII 守卫：方法结束时清理素材临时目录（仅当 -d 为 tar 包时 tempDir 非空）
        struct TempDirGuard {
            ServiceManager &sm;
            UpgradeArtifacts &arts;
            explicit TempDirGuard(ServiceManager &s, UpgradeArtifacts &a) : sm(s), arts(a) {}
            ~TempDirGuard() { sm.CleanupArtifactsTempDir(arts); }
            TempDirGuard(const TempDirGuard &) = delete;
            TempDirGuard &operator=(const TempDirGuard &) = delete;
            TempDirGuard(TempDirGuard &&) = delete;
            TempDirGuard &operator=(TempDirGuard &&) = delete;
        } tempDirGuard(*mServiceManager, artifacts);
        SLOG_INFO << "Artifacts found - servicePackage: "
                  << (artifacts.servicePackage.empty() ? "<none>" : artifacts.servicePackage)
                  << ", modelPath: " << (artifacts.modelPath.empty() ? "<none>" : artifacts.modelPath)
                  << ", nginxDir: " << (artifacts.nginxDir.empty() ? "<none>" : artifacts.nginxDir);

        // 3. 若有 model 素材：安装模型（排除当前升级服务，保留 .back 备份以便回退）
        if (!artifacts.modelPath.empty()) {
            SLOG_INFO << "Step 3: Add model (excluding service: " << serviceName << ")";
            auto modelRet =
                mServiceManager->AddModelWithBackupRetained(artifacts.modelPath, serviceName, modelInstalledName);
            if (!modelRet.IsDefalutSuccess()) {
                SLOG_ERROR << "Add model failed: " << modelRet.msg;
                restoreNginx();
                restoreService();
                writeResult(false, modelRet.msg);
                return modelRet;
            }
            SLOG_INFO << "Model added successfully: " << modelInstalledName << " (backup retained)";
        } else {
            SLOG_INFO << "Step 3: No model artifact, skip";
        }

        // 4. 若有服务包：执行服务升级，失败则回退模型
        //    注意：服务已在 Step 1.1 停止，UpdateService 内部检测到服务未运行（wasRunning=false），
        //          不会重复停止；若 keep_alive_time_sec > 0，UpdateService 会启动验证后因内部
        //          wasRunning=false 而停止服务，最终由 restoreService() 恢复运行状态。
        ResultMsg result = MakeSuccess();
        if (!artifacts.servicePackage.empty()) {
            SLOG_INFO << "Step 4: Upgrade service from package: " << artifacts.servicePackage;
            // 解析新版本号（用于结果记录，失败不影响升级流程）
            newVersion = mServiceManager->ReadVersionFromPackage(serviceName, artifacts.servicePackage);
            SLOG_INFO << "New version from package: " << (newVersion.empty() ? "<unknown>" : newVersion);

            result = UpgradeService(serviceName, artifacts.servicePackage);
            if (!result.IsDefalutSuccess()) {
                SLOG_ERROR << "Upgrade service failed: " << result.msg << ", rolling back model if any";
                // 回退模型（若已安装）
                if (!modelInstalledName.empty()) {
                    auto rollbackRet = mServiceManager->RollbackModel(modelInstalledName);
                    if (!rollbackRet.IsDefalutSuccess()) {
                        SLOG_ERROR << "Rollback model failed: " << rollbackRet.msg;
                    }
                }
                restoreNginx();
                restoreService();
                writeResult(false, result.msg);
                return result;
            }
            SLOG_INFO << "Service upgraded successfully";
        } else {
            SLOG_INFO << "Step 4: No service package, skip service upgrade";
        }

        // 5. 成功收尾：清理模型备份，更新或恢复 nginx 配置
        SLOG_INFO << "Step 5: Finalize - clean model backup, update nginx";
        if (!modelInstalledName.empty()) {
            auto cleanRet = mServiceManager->CleanModelBackup(modelInstalledName);
            if (!cleanRet.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to clean model backup: " << cleanRet.msg;
            }
        }

        if (!artifacts.nginxDir.empty()) {
            // 有 nginx 素材：使用第一个 nginx 前缀目录更新配置
            // 注意：此处失败不回滚已升级的服务/模型（核心升级已成功），仅恢复 nginx 旧配置并返回警告
            SLOG_INFO << "Updating nginx config from: " << artifacts.nginxDir;
            auto nginxRet = mServiceManager->InitNginx(artifacts.nginxDir);
            if (!nginxRet.IsDefalutSuccess()) {
                SLOG_ERROR << "InitNginx failed: " << nginxRet.msg << ", fallback to reset_nginx -n";
                restoreNginx();
                restoreService();
                std::string warnMsg = "Service upgraded successfully, but nginx config update failed: " + nginxRet.msg +
                                      " (nginx restored to previous config)";
                writeResult(false, warnMsg);
                SLOG_WARN << warnMsg;
                return MakeWarning(warnMsg);
            }
            SLOG_INFO << "Nginx config updated successfully";
        } else {
            // 无 nginx 素材：恢复 nginx 正常配置（移除 waiting.conf，使 scm_*.conf 生效）
            SLOG_INFO << "No nginx artifact, restore nginx to normal mode";
            restoreNginx();
        }

        // 恢复服务运行状态（升级前运行则启动，未运行则保持停止）
        restoreService();

        writeResult(true, "");
        SLOG_INFO << "Integrated upgrade completed successfully for service: " << serviceName;
        return MakeSuccess();
    }

    ResultMsg ServiceControl::StartService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Starting service: " << serviceName;
        auto result = mServiceManager->StartService(serviceName);
        // 新需求3.1：将 systemd 启动操作记录同步到服务日志文件
        SyncJournalToServiceLog(serviceName);
        return result;
    }

    ResultMsg ServiceControl::StopService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Stopping service: " << serviceName;
        auto result = mServiceManager->StopService(serviceName);
        // 新需求3.1：将 systemd 停止操作记录同步到服务日志文件
        SyncJournalToServiceLog(serviceName);
        return result;
    }

    ResultMsg ServiceControl::RestartService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Restarting service: " << serviceName;
        auto result = mServiceManager->RestartService(serviceName);
        // 新需求3.1：将 systemd 重启操作记录同步到服务日志文件
        SyncJournalToServiceLog(serviceName);
        return result;
    }

    ResultMsg ServiceControl::StartScmdSelf() {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Starting scmd self";
        return mServiceManager->StartScmdSelf();
    }

    ResultMsg ServiceControl::StopScmdSelf() {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Stopping scmd self";
        return mServiceManager->StopScmdSelf();
    }

    ResultMsg ServiceControl::RestartScmdSelf() {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Restarting scmd self";
        return mServiceManager->RestartScmdSelf();
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
                DeleteDatabaseUser(svc->dbInfo.dbType, dbUserName, dbPassword);
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
        std::string actualFilePath =
            utils::JoinPath(serviceDefinition.currentServiceDir, serviceDefinition.dbInfo.outputDir);
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
        std::string actualFilePath =
            utils::JoinPath(serviceDefinition.currentServiceDir, serviceDefinition.dbInfo.outputDir);
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

    ResultMsg ServiceControl::DeleteDatabaseUser(const DatabaseType &dbType, const std::string &username,
                                                 const std::string &password) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        ResultMsg result;
        if (dbType == DatabaseType::MYSQL) {
            tool::Mariadb mariadb(tool::MariadbDef {});
            return mariadb.DeleteUserAndDatabase(username, password);
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
        sd_journal* journal = nullptr;
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

        // 辅助：从 journal 当前条目获取指定字段值（去掉 "FIELD=" 前缀）
        auto getJournalField = [&journal](const char* field) -> std::string {
            const void* data = nullptr;
            size_t length = 0;
            int ret = sd_journal_get_data(journal, field, &data, &length);
            if (ret < 0) {
                return {};
            }
            size_t prefixLen = strlen(field) + 1;  // +1 跳过 '='
            return std::string(static_cast<const char*>(data) + prefixLen, length - prefixLen);
        };

        // 辅助：将微秒时间戳转为 "MMM DD HH:MM:SS" 格式（如 "Jul 09 21:41:32"）
        auto formatTimestamp = [](uint64_t usec) -> std::string {
            time_t sec = static_cast<time_t>(usec / 1000000U);
            struct tm timeInfo {};
            localtime_r(&sec, &timeInfo);
            static const std::array<const char*, 12> MonthAbbr = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            int mon = timeInfo.tm_mon;
            const char* monthStr = (mon >= 0 && mon < 12) ? MonthAbbr[static_cast<size_t>(mon)] : "???";
            std::ostringstream oss;
            oss << monthStr << " " << std::setw(2) << std::setfill('0') << timeInfo.tm_mday << " " << std::setw(2)
                << std::setfill('0') << timeInfo.tm_hour << ":" << std::setw(2) << std::setfill('0') << timeInfo.tm_min
                << ":" << std::setw(2) << std::setfill('0') << timeInfo.tm_sec;
            return oss.str();
        };

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

            // 提取各字段：时间戳、主机名、进程标识符、PID、消息正文
            std::string tsStr;
            std::string tsRaw = getJournalField("__REALTIME_TIMESTAMP");
            if (!tsRaw.empty()) {
                uint64_t usec = std::stoull(tsRaw);
                tsStr = formatTimestamp(usec);
            }

            std::string hostname = getJournalField("_HOSTNAME");
            // 进程标识符：优先 SYSLOG_IDENTIFIER，回退 _COMM
            std::string identifier = getJournalField("SYSLOG_IDENTIFIER");
            if (identifier.empty()) {
                identifier = getJournalField("_COMM");
            }
            std::string pidStr = getJournalField("_PID");
            std::string message = getJournalField("MESSAGE");
            if (message.empty()) {
                continue;  // 无 MESSAGE 则跳过
            }

            // 组装为 journalctl -o short 格式: "MMM DD HH:MM:SS hostname identifier[pid]: message"
            // 示例: "Jul 09 21:41:32 bm1684 qifeng_ca[5630]: error message..."
            std::ostringstream line;
            if (!tsStr.empty()) {
                line << tsStr << " ";
            }
            if (!hostname.empty()) {
                line << hostname << " ";
            }
            if (!identifier.empty()) {
                line << identifier;
                if (!pidStr.empty()) {
                    line << "[" << pidStr << "]";
                }
                line << ": ";
            }
            line << message;
            entries.push_back(line.str());
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

    ResultMsg ServiceControl::GetServiceLog(const std::string &serviceName, int logCount) {
        // 新需求3.1：slog 命令读取服务日志，优先读文件，回退 journal
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        int count = logCount > 0 ? logCount : 10;
        const auto &configInfo = mConfigLoader->GetConfigInfo();

        // 确定日志文件路径和 journal unit 名
        std::string logFile;
        std::string unitName;
        if (serviceName.empty()) {
            // scmd 自身：日志文件 <logsDir>/qifeng-scm/qifeng-scm.log，unit 名 qifeng-scmd
            logFile = utils::JoinPath(utils::JoinPath(configInfo.logsDir, "qifeng-scm"), "qifeng-scm.log");
            unitName = "qifeng-scmd";
        } else {
            // 校验服务已注册，避免查询任意系统服务
            if (mConfigLoader->GetServiceByName(serviceName) == nullptr) {
                return MakeError("Service not found: " + serviceName);
            }
            logFile = utils::JoinPath(utils::JoinPath(configInfo.logsDir, serviceName), serviceName + ".log");
            unitName = std::string(FileManager::GetServiceFilePrefix()) + serviceName;
        }

        // 1. 优先读取服务日志文件（StandardOutput 重定向 + journal 同步的内容）
        auto lines = qifeng::scm::utils::ReadFileLastNLines(logFile, count);
        if (!lines.empty()) {
            return ResultMsg {0, qifeng::scm::utils::JoinJournalLines(lines)};
        }

        // 2. 文件不存在或为空，回退读取 systemd journal
        SLOG_INFO << "Service log file empty or missing: " << logFile << ", fall back to journal for unit: "
                  << unitName;
        auto journalLines = qifeng::scm::utils::ReadJournalLastN(unitName, count);
        return ResultMsg {0, qifeng::scm::utils::JoinJournalLines(journalLines)};
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
        // 先清除数据库相关（在删除服务数据之前，以便读取密码文件）
        if (svc->dbInfo.dbType != DatabaseType::NONE) {
            ResultMsg result = ClearDatabaseData(*svc);
            if (result.code == -1) {
                return MakeError("clear database data failed:" + result.msg);
            }
        }
        mServiceManager->ClearServiceData(serviceName);
        return MakeSuccess();
    }

    ResultMsg ServiceControl::ClearDatabaseData(const ServiceDefinition &svc) {
        // 从密码文件读取用户密码，用于以用户身份发现所有可访问数据库
        std::string password;
        std::string dbPassFile =
            utils::JoinPath(utils::JoinPath(svc.currentServiceDir, svc.dbInfo.outputDir), svc.serviceName);
        std::ifstream ifs(dbPassFile);
        if (ifs.is_open()) {
            std::string userName;
            std::getline(ifs, userName);  // 第一行：用户名
            std::getline(ifs, password);  // 第二行：密码
            ifs.close();
        } else {
            SLOG_WARN << "Cannot read db user password file: " << dbPassFile
                      << ", will use mysql.db query only to find databases";
        }
        return DeleteDatabaseUser(svc.dbInfo.dbType, svc.serviceName, password);
    }

    ResultMsg ServiceControl::InitNginx(const std::string &dirPath) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "InitNginx from: " << dirPath;
        return mServiceManager->InitNginx(dirPath);
    }

    ResultMsg ServiceControl::ResetNginx(NginxResetMode mode) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "ResetNginx mode=" << static_cast<int>(mode);
        return mServiceManager->ResetNginx(mode);
    }

    ResultMsg ServiceControl::AddModel(const std::string &srcPath) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "AddModel from: " << srcPath;
        return mServiceManager->AddModel(srcPath);
    }

    ResultMsg ServiceControl::ClearModel(const std::string &modelName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "ClearModel name=" << modelName;
        return mServiceManager->ClearModel(modelName);
    }

    void ServiceControl::CreateServiceLogDirs() {
        // 新需求3.1：预创建日志目录，systemd 的 StandardOutput=append: 不会自动创建父目录
        const auto &configInfo = mConfigLoader->GetConfigInfo();
        namespace fs = std::filesystem;

        // 1. scmd 自身日志目录：<logsDir>/qifeng-scm/（与 scmd.log 隔离，存放 systemd 操作记录）
        std::string scmdLogDir = utils::JoinPath(configInfo.logsDir, "qifeng-scm");
        std::error_code ec;
        fs::create_directories(scmdLogDir, ec);
        if (ec) {
            SLOG_WARN << "Failed to create scmd log dir " << scmdLogDir << ": " << ec.message();
        }

        // 2. 每个已注册服务的日志目录：<logsDir>/<serviceName>/
        auto allServices = mConfigLoader->GetAllServices();
        for (const auto &svc : allServices) {
            std::string svcLogDir = utils::JoinPath(configInfo.logsDir, svc.serviceName);
            fs::create_directories(svcLogDir, ec);
            if (ec) {
                SLOG_WARN << "Failed to create service log dir " << svcLogDir << ": " << ec.message();
            }
        }
    }

    void ServiceControl::SyncJournalToServiceLog(const std::string &serviceName) {
        // 新需求3.1：将 systemd 启停操作的 journal 记录追加到服务日志文件
        // 服务日志文件路径：<logsDir>/<serviceName>/<serviceName>.log
        // 与 ServiceGenerator::WriteServiceLoggingConfig 中的 StandardOutput 路径保持一致
        if (serviceName.empty()) {
            return;
        }
        const auto *svc = mConfigLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return;
        }

        const auto &configInfo = mConfigLoader->GetConfigInfo();
        std::string logFile = utils::JoinPath(utils::JoinPath(configInfo.logsDir, serviceName), serviceName + ".log");

        // 构造 systemd 单元名（scmd_ + serviceName），与 ServiceManager::ToSystemdUnitName 一致
        std::string unitName = std::string(FileManager::GetServiceFilePrefix()) + serviceName;
        // 读取最近 20 条 journal 记录，覆盖一次启停操作的完整日志（含 systemd 自身消息）
        auto lines = qifeng::scm::utils::ReadJournalLastN(unitName, 20);
        if (lines.empty()) {
            return;
        }

        std::string content = qifeng::scm::utils::JoinJournalLines(lines);
        if (!qifeng::scm::utils::AppendToFile(logFile, content)) {
            SLOG_WARN << "Failed to append journal to service log: " << logFile;
        }
    }

}  // namespace qifeng::scm
