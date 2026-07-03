/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checker_runner.h"

#include "checker/core/context.h"
#include "checker/core/registry.h"
#include "checker/core/runner.h"
#include "qifeng_framework/common/logger.h"

#include <array>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace qifeng::scm {

    // NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
    CheckerRunner::CheckReport CheckerRunner::Run(const std::string &configPath) {
        CheckReport report;

        SLOG_INFO << "=== Self-check start (config: " << configPath << ") ===";

        // 1) 加载配置
        SelfTestConfig config = LoadConfig(configPath);

        // 2) 构建上下文
        Context ctx {config};

        // 3) 注册并构建内置检查器
        CheckerRegistry registry;
        RegisterAll(registry);
        auto checkers = registry.BuildAll();

        SLOG_INFO << "Registered " << checkers.size() << " checkers";

        // 4) 执行自检
        Runner runner;
        auto results = runner.RunAll(checkers, ctx);

        // 5) 汇总结果
        report.overallOk = true;
        report.details = Json::Value(Json::objectValue);

        for (const auto &r : results) {
            // 构建单项详情
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

            // 判定 critical 项是否 FAIL
            if (r.status == Status::FAIL && r.severity == Severity::CRITICAL) {
                report.overallOk = false;
            }
        }

        report.overallStatus = report.overallOk ? "OK" : "FAIL";

        // 6) 生成摘要
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

        // 7) 写入 JSON 报告文件
        report.reportPath = config.report_path;
        Json::Value reportJson(Json::objectValue);
        // 生成 ISO 8601 时间戳
        std::time_t now = std::time(nullptr);
        std::tm tm {};
        gmtime_r(&now, &tm);
        std::array<char, 24> timeBuf {};
        std::strftime(timeBuf.data(), timeBuf.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
        reportJson["timestamp"] = timeBuf.data();
        reportJson["overall"] = report.overallStatus;
        reportJson["checks"] = report.details;

        // 确保报告目录存在
        std::string reportDir;
        auto lastSlash = config.report_path.rfind('/');
        if (lastSlash != std::string::npos) {
            reportDir = config.report_path.substr(0, lastSlash);
        }
        if (!reportDir.empty()) {
            // 使用 std::filesystem 安全创建目录，避免 system() 调用
            std::error_code ec;
            std::filesystem::create_directories(reportDir, ec);
            if (ec) {
                SLOG_WARN << "Cannot create report directory: " << reportDir << ", error: " << ec.message();
            }
        }

        std::ofstream outFile(config.report_path, std::ios::out | std::ios::trunc);
        if (outFile) {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "  ";
            outFile << Json::writeString(builder, reportJson) << std::endl;
            SLOG_INFO << "Self-check report written to " << config.report_path;
        } else {
            SLOG_WARN << "Cannot write report to " << config.report_path;
        }

        SLOG_INFO << "=== Self-check done: " << report.overallStatus << " (" << report.summary << ") ===";

        return report;
    }

}  // namespace qifeng::scm
