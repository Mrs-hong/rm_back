/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/by_script/pcba_cheker.h"

#include <cstddef>
#include <sstream>
#include <string>

#include "common/cmd_execute.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

namespace qifeng::scm {

    namespace {

        /**
         * @brief 根据配置字符串获取严重级别枚举
         * @param severity 配置字符串，"critical" 或 "warning"
         * @return Severity 对应的严重级别，非法值回退为 WARNING
         */
        enum Severity ParseSeverity(const std::string &severity) {
            if (severity == "critical") {
                return Severity::CRITICAL;
            }
            return Severity::WARNING;
        }

        /**
         * @brief 将输出截断到最大长度，避免报告过大
         * @param output 原始输出
         * @param maxLen 最大保留字符数
         * @return std::string 截断后的输出
         */
        std::string TruncateOutput(const std::string &output, std::size_t maxLen) {
            if (output.size() <= maxLen) {
                return output;
            }
            return output.substr(0, maxLen) + "\n... [truncated]";
        }

    }  // namespace

    enum Severity PcbaChecker::Severity() const {
        // Run() 执行时会根据配置更新 mSeverity；若未执行则返回默认值 WARNING
        return mSeverity;
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    CheckResult PcbaChecker::Run(const Context &ctx) {
        const auto &cfg = ctx.config.pcba;

        // 根据配置更新严重级别，供后续报告与整体判定使用
        mSeverity = ParseSeverity(cfg.severity);

        CheckResult r(Name());
        r.severity = mSeverity;
        auto t0 = GetTimeMs();

        // 未配置脚本路径时跳过，避免误报
        if (cfg.exe_command.empty()) {
            r.status = Status::SKIPPED;
            r.message = "pcba script not configured";
            r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
            SLOG_INFO << "[pcba] " << r.message << " (" << r.elapsed_ms << "ms)";
            return r;
        }

        // 执行外部自检脚本
        CmdOptions options;
        options.timeout_ms = cfg.timeout_sec * 1000;
        auto pr = RunCmd(cfg.exe_command, cfg.args, options);

        // 保存原始信息到 details
        r.details.emplace_back("exit_code", std::to_string(pr.exit_code));
        r.details.emplace_back("timed_out", pr.timed_out ? "yes" : "no");
        r.details.emplace_back("timeout_sec", std::to_string(cfg.timeout_sec));
        r.details.emplace_back("output", TruncateOutput(pr.output, 4096));

        // 根据执行结果判定状态
        if (pr.timed_out) {
            r.status = Status::FAIL;
            r.message = "pcba script timed out";
        } else if (pr.exit_code != 0) {
            r.status = Status::FAIL;
            std::ostringstream oss;
            oss << "pcba script failed with exit code " << pr.exit_code;
            r.message = oss.str();
        } else if (cfg.parse_output) {
            // 退出码为 0 但需进一步解析输出关键字
            if (pr.output.find(cfg.fail_keyword) != std::string::npos) {
                r.status = Status::FAIL;
                r.message = "pcba output contains fail keyword";
            } else if (pr.output.find(cfg.pass_keyword) != std::string::npos) {
                r.status = Status::PASS;
                r.message = "pcba check passed";
            } else {
                r.status = Status::FAIL;
                r.message = "pcba output missing pass keyword";
            }
        } else {
            // 不解析输出，直接以退出码 0 判定通过
            r.status = Status::PASS;
            r.message = "pcba check passed";
        }

        r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
        SLOG_INFO << "[pcba] " << r.message << " (" << r.elapsed_ms << "ms)";
        return r;
    }

}  // namespace qifeng::scm
