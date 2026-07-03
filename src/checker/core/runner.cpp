/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/core/runner.h"

#include <chrono>
#include <future>
#include <vector>

#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

namespace {

/**
 * @brief 执行单个 checker 并强制填入耗时与名称
 */
CheckResult RunOne(IChecker &c, const Context &ctx) {
    CheckResult r(c.Name());
    try {
        r = c.Run(ctx);
        r.item = c.Name();      // 保证 item 正确
        r.severity = c.Severity(); // 填充严重级别，供整体判定使用
        if (r.message.empty()) {
            r.message = StatusToString(r.status);
        }
    } catch (const std::exception &e) {
        r.status = Status::FAIL;
        r.message = std::string("exception: ") + e.what();
    } catch (...) {
        r.status = Status::FAIL;
        r.message = "unknown exception";
    }
    return r;
}

}  // namespace

std::vector<CheckResult> Runner::RunAll(const std::vector<CheckerPtr> &checkers,
                                        const Context &ctx) {
    std::vector<CheckResult> results;
    results.reserve(checkers.size());
    int timeoutSec = ctx.config.per_item_timeout_sec;
    if (timeoutSec <= 0) {
        timeoutSec = 5;
    }

    if (!ctx.config.parallel) {
        // 顺序模式：仍保留超时包裹
        for (const auto &c : checkers) {
            auto fut = std::async(std::launch::async, [&] { return RunOne(*c, ctx); });
            if (fut.wait_for(std::chrono::seconds(timeoutSec)) != std::future_status::timeout) {
                results.push_back(fut.get());
            } else {
                CheckResult r(c->Name());
                r.status = Status::FAIL;
                r.message = "timeout";
                results.push_back(r);
                SLOG_WARN << "[" << c->Name() << "] check timeout (" << timeoutSec << "s)";
            }
        }
        return results;
    }

    // 并发模式：每个 checker 起一个任务
    std::vector<std::future<CheckResult>> futs;
    futs.reserve(checkers.size());
    for (const auto &c : checkers) {
        futs.emplace_back(std::async(std::launch::async, [&c, &ctx] { return RunOne(*c, ctx); }));
    }
    for (size_t i = 0; i < checkers.size(); ++i) {
        if (futs[i].wait_for(std::chrono::seconds(timeoutSec)) != std::future_status::timeout) {
            results.push_back(futs[i].get());
        } else {
            CheckResult r(checkers[i]->Name());
            r.status = Status::FAIL;
            r.message = "timeout";
            results.push_back(r);
            SLOG_WARN << "[" << checkers[i]->Name() << "] check timeout (" << timeoutSec << "s)";
        }
    }
    return results;
}

}  // namespace qifeng::scm