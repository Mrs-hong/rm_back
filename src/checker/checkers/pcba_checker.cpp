/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/pcba_checker.h"

#include <cstddef>
#include <sstream>
#include <string>

#include <chrono>

#include "common/cmd_execute.h"
#include "qifeng_framework/common/logger.h"

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

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    void PcbaChecker::ParseConfig(const Json::Value &j, PcbaConfig &cfg) {
        cfg.exe_command = j.isMember("exe_command") && j["exe_command"].isString()
                              ? j["exe_command"].asString()
                              : cfg.exe_command;
        if (j.isMember("args") && j["args"].isArray()) {
            cfg.args.clear();
            for (Json::ArrayIndex i = 0; i < j["args"].size(); ++i) {
                const Json::Value &v = j["args"][i];
                if (v.isString()) {
                    cfg.args.push_back(v.asString());
                }
            }
        }
        cfg.severity =
            j.isMember("severity") && j["severity"].isString() ? j["severity"].asString() : cfg.severity;
        cfg.timeout_sec = j.isMember("timeout_sec") && j["timeout_sec"].isInt() ? j["timeout_sec"].asInt()
                                                                                 : cfg.timeout_sec;
        cfg.parse_output = j.isMember("parse_output") && j["parse_output"].isBool()
                               ? j["parse_output"].asBool()
                               : cfg.parse_output;
        cfg.pass_keyword = j.isMember("pass_keyword") && j["pass_keyword"].isString()
                               ? j["pass_keyword"].asString()
                               : cfg.pass_keyword;
        cfg.fail_keyword = j.isMember("fail_keyword") && j["fail_keyword"].isString()
                               ? j["fail_keyword"].asString()
                               : cfg.fail_keyword;

        // 根据配置更新严重级别，供报告与整体判定使用
        mSeverity = ParseSeverity(cfg.severity);
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    CheckResult PcbaChecker::Run() {
        CheckResult r(Name());
        auto t0 = std::chrono::steady_clock::now();

        auto elapsed = [&]() {
            return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - t0)
                                        .count());
        };

        // 未配置脚本路径时跳过，避免误报
        if (mConfig.exe_command.empty()) {
            r.status = Status::SKIPPED;
            r.message = "pcba script not configured";
            r.elapsed_ms = elapsed();
            SLOG_INFO << "[pcba] " << r.message << " (" << r.elapsed_ms << "ms)";
            return r;
        }

        // 执行外部自检脚本
        CmdOptions options;
        options.timeout_ms = mConfig.timeout_sec * 1000;
        auto pr = RunCmd(mConfig.exe_command, mConfig.args, options);

        // 保存原始信息到 details
        r.details.emplace_back("exit_code", std::to_string(pr.exit_code));
        r.details.emplace_back("timed_out", pr.timed_out ? "yes" : "no");
        r.details.emplace_back("timeout_sec", std::to_string(mConfig.timeout_sec));
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
        } else if (mConfig.parse_output) {
            // 退出码为 0 但需进一步解析输出关键字
            if (pr.output.find(mConfig.fail_keyword) != std::string::npos) {
                r.status = Status::FAIL;
                r.message = "pcba output contains fail keyword";
            } else if (pr.output.find(mConfig.pass_keyword) != std::string::npos) {
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

        r.elapsed_ms = elapsed();
        SLOG_INFO << "[pcba] " << r.message << " (" << r.elapsed_ms << "ms)";
        return r;
    }

}  // namespace qifeng::scm
