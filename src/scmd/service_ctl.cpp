/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/scmd_types.h"
#include "common/types.h"
#include "scmd/service_ctl.h"

#include "common/config.h"
#include "common/utils.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/database_service.h"
#include "service_manger/file_manager.h"
#include "service_manger/model_manager.h"
#include "service_manger/nginx_manager.h"
#include "service_manger/service_manager.h"
#include "service_manger/upgrade_service.h"

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
        Logger::GetInstance().Initialize(configInfo.logsDir, "scmd.log", logFileSizeBytes, configInfo.logFileCount);
        SLOG_INFO << "ServiceControl initializing...";

        mServiceManager = std::make_shared<ServiceManager>(mConfigLoader);

        // 构造共享依赖上下文和领域管理器
        // 注意：mContext 需作为成员保活，因为 NginxManager/ModelManager/UpgradeService/DatabaseService
        //       内部以 const ServiceContext& 引用持有它
        mContext = mServiceManager->GetServiceContext();
        mDatabaseService = std::make_shared<DatabaseService>(mContext);
        mNginxManager = std::make_shared<NginxManager>(mContext, *mServiceManager);
        mModelManager = std::make_shared<ModelManager>(mContext, *mServiceManager);
        mUpgradeService = std::make_shared<class UpgradeService>(mContext, *mServiceManager, *mModelManager,
                                                                  *mNginxManager, *mDatabaseService);

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

        result = mDatabaseService->InitServiceDatabase(actualServiceName);
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
            ResultMsg result = mDatabaseService->ClearDatabaseData(*svc);
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
        auto result = mUpgradeService->UpdateService(serviceName, serviceTarPath);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        // 确认升级完成，清理旧版本备份
        auto cleanResult = mUpgradeService->CleanUpgradeBackup(serviceName);
        if (!cleanResult.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to clean upgrade backup: " << cleanResult.msg;
        }

        return MakeSuccess();
    }

    ResultMsg ServiceControl::PerformInternalUpgrade(const std::string &serviceName) {
        // 向后兼容：转调一体化升级，使用服务内部 soft_dir 作为素材来源
        return PerformIntegratedUpgrade(serviceName, "");
    }

    ResultMsg ServiceControl::PerformIntegratedUpgrade(const std::string &serviceName, const std::string &tarDir) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        // 委托给 UpgradeService：编排逻辑（nginx 切换、素材查找、模型安装、服务升级、收尾）已移至该领域管理器
        return mUpgradeService->PerformIntegratedUpgrade(serviceName, tarDir);
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

    ResultMsg ServiceControl::StopAllServices() {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Stopping all services";
        return mServiceManager->StopAllServices();
    }

    ResultMsg ServiceControl::RestartService(const std::string &serviceName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Restarting service: " << serviceName;
        return mServiceManager->RestartService(serviceName);
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

    ResultMsg ServiceControl::UninstallAllServices() {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "Uninstalling all services";

        // 获取所有已安装服务，按逆序逐个卸载（后安装的先卸载，尽量保证依赖关系正确）
        auto allServices = mConfigLoader->GetAllServices();
        if (allServices.empty()) {
            SLOG_INFO << "No services installed, nothing to uninstall";
            return MakeSuccess();
        }

        int failCount = 0;
        std::string lastError;
        for (auto it = allServices.rbegin(); it != allServices.rend(); ++it) {
            const auto &svcName = it->serviceName;
            SLOG_INFO << "Uninstalling service: " << svcName;
            auto result = UninstallService(svcName);
            if (!result.IsDefalutSuccess()) {
                ++failCount;
                lastError = svcName + ": " + result.msg;
                SLOG_WARN << "Failed to uninstall service " << svcName << ": " << result.msg;
            }
        }

        if (failCount > 0) {
            return MakeWarning("Uninstalled " + std::to_string(allServices.size() - static_cast<size_t>(failCount))
                               + "/" + std::to_string(allServices.size()) + " services, last error: " + lastError);
        }
        SLOG_INFO << "All services uninstalled successfully";
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
             static const std::array<const char*, 12> MonthAbbr = {
                 "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
             };
             int mon = timeInfo.tm_mon;
             const char* monthStr = (mon >= 0 && mon < 12) ? MonthAbbr[static_cast<size_t>(mon)] : "???";
             std::ostringstream oss;
             oss << monthStr << " "
                 << std::setw(2) << std::setfill('0') << timeInfo.tm_mday << " "
                 << std::setw(2) << std::setfill('0') << timeInfo.tm_hour << ":"
                 << std::setw(2) << std::setfill('0') << timeInfo.tm_min << ":"
                 << std::setw(2) << std::setfill('0') << timeInfo.tm_sec;
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
            ResultMsg result = mDatabaseService->ClearDatabaseData(*svc);
            if (result.code == -1) {
                return MakeError("clear database data failed:" + result.msg);
            }
        }
        mServiceManager->ClearServiceData(serviceName);
        return MakeSuccess();
    }

    ResultMsg ServiceControl::InitNginx(const std::string &dirPath) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "InitNginx from: " << dirPath;
        return mNginxManager->InitNginx(dirPath);
    }

    ResultMsg ServiceControl::ResetNginx(NginxResetMode mode) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "ResetNginx mode=" << static_cast<int>(mode);
        return mNginxManager->ResetNginx(mode);
    }

    ResultMsg ServiceControl::AddModel(const std::string &srcPath) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "AddModel from: " << srcPath;
        return mModelManager->AddModel(srcPath);
    }

    ResultMsg ServiceControl::ClearModel(const std::string &modelName) {
        if (!mIsInit) {
            return MakeError("ServiceControl is not initialized");
        }
        SLOG_INFO << "ClearModel name=" << modelName;
        return mModelManager->ClearModel(modelName);
    }

}  // namespace qifeng::scm
