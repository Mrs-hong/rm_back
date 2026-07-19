/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/service_operations.h"

#include "common/config.h"
#include "common/types.h"
#include "common/utils/journal.h"
#include "common/utils/path.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/database_service.h"
#include "service_manager/file_manager.h"
#include "service_manager/service_manager.h"

#include <fstream>
#include <vector>

namespace qifeng::scm::service_operations {

    ResultMsg InstallServiceWithDb(const ServiceContext &ctx, const std::string &serviceName,
                                   const std::string &tarPath) {
        SLOG_INFO << "Installing service: " << serviceName << " from " << tarPath;
        auto result = ctx.serviceManager->InstallService(tarPath, serviceName);
        if (result.code != 0 && result.code != 1) {
            // 安装失败（非警告），直接返回错误
            SLOG_ERROR << "Failed to install service: " << serviceName << " from " << tarPath
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

        result = ctx.databaseService->InitServiceDatabase(actualServiceName);
        if (!result.IsDefaultSuccess()) {
            // 数据建库操作失败，回滚安装
            UninstallServiceWithCleanup(ctx, actualServiceName);
            return MakeError("install service " + actualServiceName + " failed: " + result.msg);
        }

        // 数据库初始化成功后，若存在验证警告则返回警告（code=1），否则返回成功
        if (!verifyWarning.empty()) {
            return MakeWarning(verifyWarning);
        }
        return MakeSuccess();
    }

    ResultMsg UninstallServiceWithCleanup(const ServiceContext &ctx, const std::string &serviceName) {
        SLOG_INFO << "Uninstalling service: " << serviceName;
        auto svc = ctx.configLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }

        // 先清除数据库相关（在删除服务文件之前，以便读取密码文件发现所有数据库）
        if (svc->dbInfo.dbType != DatabaseType::NONE) {
            ResultMsg result = ctx.databaseService->ClearDatabaseData(*svc);
            if (result.code == -1) {
                return MakeError("clear database data failed:" + result.msg);
            }
        }

        // 卸载服务（停止服务 + 删除服务文件 + 从配置中移除）
        ResultMsg ret = ctx.serviceManager->UninstallService(serviceName);
        if (ret.code == -1) {
            return MakeError("uninstall service failed:" + ret.msg);
        }
        return ret;
    }

    ResultMsg RestartAllServices(const ServiceContext &ctx) {
        SLOG_INFO << "Restarting all services";

        // 先停止所有服务
        auto stopResult = ctx.serviceManager->StopAllServices();
        if (!stopResult.IsDefaultSuccess()) {
            SLOG_WARN << "Some services failed to stop: " << stopResult.msg;
        }

        // 再启动所有 autoStart 服务
        auto startResult = ctx.serviceManager->StartAllAutoStartServices();
        if (!startResult.IsDefaultSuccess()) {
            SLOG_WARN << "Some services failed to start: " << startResult.msg;
        }

        if (!stopResult.IsDefaultSuccess() || !startResult.IsDefaultSuccess()) {
            return MakeWarning("Some services failed during restart");
        }

        SLOG_INFO << "All services restarted successfully";
        return MakeSuccess();
    }

    ResultMsg GetOperationLog(const ServiceContext &ctx, int /*logLevel*/, int logCount) {
        const auto &configInfo = ctx.configLoader->GetConfigInfo();
        std::string logPath = utils::JoinPath(configInfo.logsDir, "scmd.log");

        std::ifstream logFile(logPath);
        if (!logFile.is_open()) {
            return MakeError("Log file not found: " + logPath);
        }

        // 读取所有行，返回最后 logCount 行内容
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(logFile, line)) {
            lines.push_back(line);
        }
        logFile.close();

        int startIdx =
            logCount > 0 && static_cast<int>(lines.size()) > logCount ? static_cast<int>(lines.size()) - logCount : 0;

        // 将日志内容拼接到 msg 中
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

    ResultMsg GetServiceLog(const ServiceContext &ctx, const std::string &serviceName, int logCount) {
        // 新需求3.1：slog 命令读取服务日志，优先读文件，回退 journal
        int count = logCount > 0 ? logCount : 10;
        const auto &configInfo = ctx.configLoader->GetConfigInfo();

        // 确定日志文件路径和 journal unit 名
        std::string logFile;
        std::string unitName;
        if (serviceName.empty()) {
            // scmd 自身：日志文件 <logsDir>/qifeng-scm/qifeng-scm.log，unit 名 qifeng-scmd
            logFile = utils::JoinPath(utils::JoinPath(configInfo.logsDir, "qifeng-scm"), "qifeng-scm.log");
            unitName = "qifeng-scmd";
        } else {
            // 校验服务已注册，避免查询任意系统服务
            if (ctx.configLoader->GetServiceByName(serviceName) == nullptr) {
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

}  // namespace qifeng::scm::service_operations