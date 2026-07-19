/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/journal.h"

#include "qifeng_framework/common/logger.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <systemd/sd-journal.h>

namespace qifeng::scm::utils {

    // 辅助：从 journal 当前条目获取指定字段值（去掉 "FIELD=" 前缀）
    static std::string GetJournalField(sd_journal* journal, const char* field) {
        const void* data = nullptr;
        size_t length = 0;
        int ret = sd_journal_get_data(journal, field, &data, &length);  // NOLINT (systemd C API)
        if (ret < 0) {
            return {};
        }
        size_t prefixLen = std::strlen(field) + 1;  // +1 跳过 '='
        return std::string(static_cast<const char*>(data) + prefixLen, length - prefixLen);
    }

    // 辅助：将微秒时间戳转为 "MMM DD HH:MM:SS" 格式（如 "Jul 09 21:41:32"）
    static std::string FormatJournalTimestamp(uint64_t usec) {
        time_t sec = static_cast<time_t>(usec / 1000000U);
        struct tm timeInfo {};
        localtime_r(&sec, &timeInfo);  // NOLINT (POSIX C API)
        static const std::array<const char*, 12> MonthAbbr = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        int mon = timeInfo.tm_mon;
        const char* monthStr = (mon >= 0 && mon < 12) ? MonthAbbr[static_cast<size_t>(mon)] : "???";
        std::ostringstream oss;
        oss << monthStr << " " << std::setw(2) << std::setfill('0') << timeInfo.tm_mday << " " << std::setw(2)
            << std::setfill('0') << timeInfo.tm_hour << ":" << std::setw(2) << std::setfill('0') << timeInfo.tm_min
            << ":" << std::setw(2) << std::setfill('0') << timeInfo.tm_sec;
        return oss.str();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    std::vector<std::string> ReadJournalLastN(const std::string &unitName, int count) {
        if (unitName.empty()) {
            return {};
        }
        // 行数边界处理：<=0 时使用默认 10
        int maxCount = count > 0 ? count : 10;

        // sd-journal 句柄使用 RAII 确保释放
        sd_journal* journal = nullptr;
        auto cleanup = [&journal]() {
            if (journal != nullptr) {
                sd_journal_close(journal);  // NOLINT (systemd C API)
                journal = nullptr;
            }
        };

        // 打开本地 journal
        int r = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY);  // NOLINT (systemd C API)
        if (r < 0) {
            SLOG_ERROR << "Failed to open journal: " << strerror(-r);
            return {};
        }

        // 添加单元过滤条件：_SYSTEMD_UNIT=<unit>.service
        std::string match = "_SYSTEMD_UNIT=" + unitName + ".service";
        r = sd_journal_add_match(journal, match.c_str(), 0);  // NOLINT (systemd C API)
        if (r < 0) {
            SLOG_ERROR << "Failed to add journal match: " << strerror(-r);
            cleanup();
            return {};
        }

        // 跳到日志末尾，向前读取最近 maxCount 条
        r = sd_journal_seek_tail(journal);  // NOLINT (systemd C API)
        if (r < 0) {
            SLOG_ERROR << "Failed to seek journal tail: " << strerror(-r);
            cleanup();
            return {};
        }

        // entries 按从新到旧收集，最后反转为从旧到新（与 journalctl -n 输出顺序一致）
        std::vector<std::string> entries;
        entries.reserve(static_cast<size_t>(maxCount));

        while (static_cast<int>(entries.size()) < maxCount) {
            r = sd_journal_previous(journal);  // NOLINT (systemd C API)
            if (r == 0) {
                break;  // 到达日志开头
            }
            if (r < 0) {
                SLOG_ERROR << "Failed to iterate journal: " << strerror(-r);
                cleanup();
                return {};
            }

            // 提取各字段：时间戳、主机名、进程标识符、PID、消息正文
            std::string tsStr;
            std::string tsRaw = GetJournalField(journal, "__REALTIME_TIMESTAMP");
            if (!tsRaw.empty()) {
                uint64_t usec = std::stoull(tsRaw);
                tsStr = FormatJournalTimestamp(usec);
            }

            std::string hostname = GetJournalField(journal, "_HOSTNAME");
            // 进程标识符：优先 SYSLOG_IDENTIFIER，回退 _COMM
            std::string identifier = GetJournalField(journal, "SYSLOG_IDENTIFIER");
            if (identifier.empty()) {
                identifier = GetJournalField(journal, "_COMM");
            }
            std::string pidStr = GetJournalField(journal, "_PID");
            std::string message = GetJournalField(journal, "MESSAGE");
            if (message.empty()) {
                continue;  // 无 MESSAGE 则跳过
            }

            // 组装为 journalctl -o short 格式: "MMM DD HH:MM:SS hostname identifier[pid]: message"
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

        // 反转为从旧到新
        std::reverse(entries.begin(), entries.end());
        return entries;
    }

    std::string JoinJournalLines(const std::vector<std::string> &lines) {
        std::string output;
        for (const auto &line : lines) {
            output += line + "\n";
        }
        return output;
    }

    std::vector<std::string> ReadFileLastNLines(const std::string &filePath, int count) {
        std::vector<std::string> lines;
        std::ifstream file(filePath);
        if (!file.is_open()) {
            return {};
        }

        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        file.close();

        // 返回最后 count 行
        if (count > 0 && static_cast<int>(lines.size()) > count) {
            lines.erase(lines.begin(), lines.begin() + (static_cast<int>(lines.size()) - count));
        }
        return lines;
    }

    bool AppendToFile(const std::string &filePath, const std::string &content) {
        // 确保父目录存在
        std::filesystem::path parentPath = std::filesystem::path(filePath).parent_path();
        if (!parentPath.empty() && !std::filesystem::exists(parentPath)) {
            std::error_code ec;
            std::filesystem::create_directories(parentPath, ec);
            if (ec) {
                SLOG_ERROR << "Failed to create directory " << parentPath.string() << ": " << ec.message();
                return false;
            }
        }

        std::ofstream ofs(filePath, std::ios::app);
        if (!ofs.is_open()) {
            SLOG_ERROR << "Failed to open file for append: " << filePath;
            return false;
        }
        ofs << content;
        return ofs.good();
    }

}  // namespace qifeng::scm::utils
