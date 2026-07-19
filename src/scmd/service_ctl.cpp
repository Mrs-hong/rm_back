/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/scmd_types.h"
#include "common/types.h"
#include "scmd/service_ctl.h"

#include "common/config.h"
#include "common/utils/journal.h"
#include "common/utils/path.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/database_service.h"
#include "service_manager/file_manager.h"
#include "service_manager/model_manager.h"
#include "service_manager/nginx_manager.h"
#include "service_manager/service_manager.h"
#include "service_manager/upgrade_service.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <systemd/sd-journal.h>

namespace qifeng::scm {

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceControl::Init() {
        mConfigLoader = std::make_shared<ConfigLoader>();
        auto result = mConfigLoader->Initialize();
        if (!result.IsDefaultSuccess() && result.code != 1) {
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

        // 构造 ServiceContext：从 ServiceManager 获取基础依赖上下文（configLoader/fileManager/dbusManager），
        // 再回填 serviceManager 指针，形成完整的共享依赖上下文
        mContext = mServiceManager->GetServiceContext();
        mContext.serviceManager = mServiceManager;

        // 按依赖顺序构造 4 个领域 Manager（DB → Nginx → Model → Upgrade），每个构造后立即填回 mContext
        // 依赖关系：UpgradeService 依赖 ModelManager/NginxManager/DatabaseService，故最后构造
        mDatabaseService = std::make_shared<DatabaseService>(mContext);
        mContext.databaseService = mDatabaseService;

        mNginxManager = std::make_shared<NginxManager>(mContext, *mServiceManager);
        mContext.nginxManager = mNginxManager;

        mModelManager = std::make_shared<ModelManager>(mContext, *mServiceManager);
        mContext.modelManager = mModelManager;

        mUpgradeService = std::make_shared<UpgradeService>(mContext, *mServiceManager, *mModelManager, *mNginxManager,
                                                           *mDatabaseService);
        mContext.upgradeService = mUpgradeService;

        // 升级兼容：启动时重新生成已安装服务的 .service 文件，
        // 应用新增的 StandardOutput/StandardError/SyslogIdentifier/LimitCORE 配置
        auto regenResult = mServiceManager->RegenerateAllServiceFiles();
        if (!regenResult.IsDefaultSuccess()) {
            SLOG_WARN << "Some service files failed to regenerate: " << regenResult.msg;
        }

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

        mIsInit = true;
        SLOG_INFO << "ServiceControl initialized successfully";
        return MakeSuccess();
    }

    const ConfigLoader &ServiceControl::GetConfigLoader() const {
        return *mConfigLoader;
    }

    const ServiceContext &ServiceControl::GetServiceContext() const {
        return mContext;
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
        if (!stopResult.IsDefaultSuccess()) {
            SLOG_WARN << "Some services failed to stop: " << stopResult.msg;
        }

        // 再启动所有autoStart服务
        auto startResult = mServiceManager->StartAllAutoStartServices();
        if (!startResult.IsDefaultSuccess()) {
            SLOG_WARN << "Some services failed to start: " << startResult.msg;
        }

        if (!stopResult.IsDefaultSuccess() || !startResult.IsDefaultSuccess()) {
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
        SLOG_INFO << "Service log file empty or missing: " << logFile
                  << ", fall back to journal for unit: " << unitName;
        auto journalLines = qifeng::scm::utils::ReadJournalLastN(unitName, count);
        return ResultMsg {0, qifeng::scm::utils::JoinJournalLines(journalLines)};
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
        const auto* svc = mConfigLoader->GetServiceByName(serviceName);
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
