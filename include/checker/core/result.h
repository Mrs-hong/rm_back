/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace qifeng::scm {

// 自检状态。SKIPPED 表示所需资源(设备/库/SDK)不可用，与 FAIL 区分：
//   SKIPPED = 硬件未装配/库未安装；FAIL = 硬件存在但自检异常。
enum class Status {
    PASS,     // 通过
    FAIL,     // 失败（硬件存在但异常）
    WARNING,  // 告警（非阻断）
    SKIPPED,  // 跳过（资源不可用）
};

// 严重级别：决定该检查器失败是否阻断设备启动(overall=FAIL)。
enum class Severity {
    CRITICAL,  // 关键：失败则整体 FAIL
    WARNING,   // 告警：失败仅记录，不影响 overall
};

/**
 * @brief 单项自检结果
 */
struct CheckResult {
    std::string item;                                         // 检查项名，如 "disk"
    Status status = Status::SKIPPED;                          // 最终状态
    Severity severity = Severity::WARNING;                    // 严重级别
    int elapsed_ms = 0;                                       // 耗时(毫秒)
    std::string message;                                      // 一句话结论
    std::vector<std::pair<std::string, std::string>> details; // 附加明细 k-v

    CheckResult() = default;

    /**
     * @brief 便捷构造：立即填充 item 与状态
     */
    explicit CheckResult(std::string n) : item(std::move(n)) {}
};

/**
 * @brief 状态转字符串，用于日志与 JSON 报告
 */
inline const char *StatusToString(Status s) {
    switch (s) {
        case Status::PASS:    return "PASS";
        case Status::FAIL:    return "FAIL";
        case Status::WARNING: return "WARN";
        case Status::SKIPPED: return "SKIP";
        default:              return "UNKNOWN";
    }
}

/**
 * @brief 严重级别转字符串，用于日志与 JSON 报告
 */
inline const char *SeverityToString(Severity sv) {
    return sv == Severity::CRITICAL ? "critical" : "warning";
}

}  // namespace qifeng::scm