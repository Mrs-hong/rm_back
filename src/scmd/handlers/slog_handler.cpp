/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/slog_handler.h"

#include "common/config.h"
#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/file_manager.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <systemd/sd-journal.h>

namespace qifeng::scm {

    static std::optional<SlogRequest> FromJson(const Json::Value& params) {
        SlogRequest req;
        if (params.isMember("serviceName") && params["serviceName"].isString()) {
            req.serviceName = params["serviceName"].asString();
        }
        if (params.isMember("logCount") && params["logCount"].isInt()) {
            req.logCount = params["logCount"].asInt();
        }
        return req;
    }

    ScmCommand SlogHandler::GetCommand() const {
        return ScmCommand::SLOG;
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ScmResponse SlogHandler::Handle(const ScmRequest& request,
                                    const ServiceContext& ctx,
                                    KeyOperationRecorder& /*recorder*/) {
        auto paramsOpt = FromJson(request.params);
        if (!paramsOpt.has_value() || paramsOpt->serviceName.empty()) {
            SLOG_WARN << "Slog command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Slog command requires service name";
            return response;
        }

        const auto& params = *paramsOpt;

        // 校验服务已注册，避免查询任意系统服务
        if (ctx.configLoader->GetServiceByName(params.serviceName) == nullptr) {
            ScmResponse response;
            response.code = -1;
            response.message = "Service not found: " + params.serviceName;
            return response;
        }

        // 行数边界处理：<=0 时使用默认 10
        int count = params.logCount > 0 ? params.logCount : 10;

        // 构造 systemd 单元名（scmd_ + serviceName），与 ServiceManager::ToSystemdUnitName 规则一致
        std::string unitName = std::string(FileManager::GetServiceFilePrefix()) + params.serviceName;

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
            ScmResponse response;
            response.code = -1;
            response.message = "Failed to open journal: " + std::string(strerror(-r));
            return response;
        }

        // 添加单元过滤条件：_SYSTEMD_UNIT=<unit>.service
        std::string match = "_SYSTEMD_UNIT=" + unitName + ".service";
        r = sd_journal_add_match(journal, match.c_str(), 0);
        if (r < 0) {
            SLOG_ERROR << "Failed to add journal match: " << strerror(-r);
            cleanup();
            ScmResponse response;
            response.code = -1;
            response.message = "Failed to add journal match: " + std::string(strerror(-r));
            return response;
        }

        // 跳到日志末尾，向前读取最近 count 条
        r = sd_journal_seek_tail(journal);
        if (r < 0) {
            SLOG_ERROR << "Failed to seek journal tail: " << strerror(-r);
            cleanup();
            ScmResponse response;
            response.code = -1;
            response.message = "Failed to seek journal tail: " + std::string(strerror(-r));
            return response;
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
                ScmResponse response;
                response.code = -1;
                response.message = "Failed to iterate journal: " + std::string(strerror(-r));
                return response;
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
            std::ostringstream lineStream;
            if (!tsStr.empty()) {
                lineStream << tsStr << " ";
            }
            if (!hostname.empty()) {
                lineStream << hostname << " ";
            }
            if (!identifier.empty()) {
                lineStream << identifier;
                if (!pidStr.empty()) {
                    lineStream << "[" << pidStr << "]";
                }
                lineStream << ": ";
            }
            lineStream << message;
            entries.push_back(lineStream.str());
        }

        cleanup();

        // 反转为从旧到新，逐行拼接（每条一行，末尾保留换行）
        std::reverse(entries.begin(), entries.end());
        std::string output;
        for (const auto& entry : entries) {
            output += entry + "\n";
        }

        ScmResponse response;
        response.code = 0;
        response.message = output;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::SLOG, SlogHandler)

}  // namespace qifeng::scm
