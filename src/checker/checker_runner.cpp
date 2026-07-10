/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checker_runner.h"

#include <array>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "checker/core/registry.h"
#include "checker/core/runner.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    // NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
    CheckerRunner::CheckReport CheckerRunner::Run(const JsonLoad &loader) {
        CheckReport report;

        SLOG_INFO << "=== Self-check start ===";

        // 1) 注册并构建启用的 checker
        CheckerRegistry registry;
        RegisterAll(registry);
        auto checkers = registry.BuildAll(loader.Root());

        SLOG_INFO << "Self-check built " << checkers.size() << " checkers";

        // 2) 读取全局配置
        const int perItemTimeoutSec = loader.GetOr<int>("per_item_timeout_sec", 5);
        const bool parallel = loader.GetOr<bool>("parallel", true);
        const std::string reportPath = loader.GetOr<std::string>("report_path",
                                                                  "/var/log/qifeng-scm/selftest-report.json");

        // 3) 执行自检
        Runner runner;
        auto results = runner.RunAll(checkers, perItemTimeoutSec, parallel);

        // 4) 汇总结果
        report.overallOk = true;
        report.details = Json::Value(Json::objectValue);

        for (const auto &r : results) {
            Json::Value item(Json::objectValue);
            item["status"] = StatusToString(r.status);
            item["severity"] = SeverityToString(r.severity);
            item["elapsed_ms"] = r.elapsed_ms;
            item["message"] = r.message;

            Json::Value detailObj(Json::objectValue);
            for (const auto &kv : r.details) {
                detailObj[kv.first] = kv.second;
            }
            item["details"] = std::move(detailObj);

            report.details[r.item] = std::move(item);

            // critical 项 FAIL → overallOk = false
            if (r.status == Status::FAIL && r.severity == Severity::CRITICAL) {
                report.overallOk = false;
            }
        }

        report.overallStatus = report.overallOk ? "OK" : "FAIL";

        // 5) 生成摘要
        int passCount = 0;
        int failCount = 0;
        int skipCount = 0;
        int warnCount = 0;
        for (const auto &r : results) {
            switch (r.status) {
                case Status::PASS:
                    ++passCount;
                    break;
                case Status::FAIL:
                    ++failCount;
                    break;
                case Status::SKIPPED:
                    ++skipCount;
                    break;
                case Status::WARNING:
                    ++warnCount;
                    break;
                default:
                    break;
            }
        }
        std::ostringstream oss;
        oss << passCount << " passed, " << failCount << " failed, " << warnCount << " warnings, " << skipCount
            << " skipped";
        report.summary = oss.str();

        // 6) 写入 JSON 报告文件
        report.reportPath = reportPath;
        Json::Value reportJson(Json::objectValue);
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        gmtime_r(&now, &tm);
        std::array<char, 24> timeBuf{};
        std::strftime(timeBuf.data(), timeBuf.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
        reportJson["timestamp"] = timeBuf.data();
        reportJson["overall"] = report.overallStatus;
        reportJson["checks"] = report.details;

        // 确保报告目录存在
        std::string reportDir;
        auto lastSlash = reportPath.rfind('/');
        if (lastSlash != std::string::npos) {
            reportDir = reportPath.substr(0, lastSlash);
        }
        if (!reportDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(reportDir, ec);
            if (ec) {
                SLOG_WARN << "[runner] Cannot create report directory: " << reportDir
                          << ", error: " << ec.message();
            }
        }

        std::ofstream outFile(reportPath, std::ios::out | std::ios::trunc);
        if (outFile) {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "  ";
            builder["emitUTF8"] = true;  // 中文直接输出 UTF-8，而非 \uXXXX 转义
            outFile << Json::writeString(builder, reportJson) << std::endl;
            SLOG_INFO << "[runner] Self-check report written to " << reportPath;
        } else {
            SLOG_WARN << "[runner] Cannot write report to " << reportPath;
        }

        SLOG_INFO << "=== Self-check done: " << report.overallStatus
                  << " (" << report.summary << ") ===";

        return report;
    }

}  // namespace qifeng::scm
