// =============================================================================
// runner.cpp — 并发调度器（带超时）
// =============================================================================
#include "checker/core/runner.h"

#include <chrono>
#include <future>
#include <utility>
#include <vector>

#include "checker/log/logger.hpp"

namespace checker {

namespace {

// 执行单个 checker 并强制填入耗时与名称
CheckResult run_one(IChecker& c, const Context& ctx) {
  CheckResult r(c.name());
  try {
    r = c.run(ctx);
    r.item = c.name();  // 保证 item 正确
    if (r.message.empty()) {
      r.message = status_to_string(r.status);
    }
  } catch (const std::exception& e) {
    r.status = Status::kFail;
    r.message = std::string("exception: ") + e.what();
  } catch (...) {
    r.status = Status::kFail;
    r.message = "unknown exception";
  }
  return r;
}

}  // namespace

std::vector<CheckResult> Runner::run_all(const std::vector<CheckerPtr>& checkers,
                                         const Context& ctx) {
  std::vector<CheckResult> results;
  results.reserve(checkers.size());
  int timeout_sec = ctx.config.per_item_timeout_sec;
  if (timeout_sec <= 0) timeout_sec = 5;

  if (!ctx.config.parallel) {
    // 顺序模式：仍保留超时包裹
    for (const auto& c : checkers) {
      auto fut = std::async(std::launch::async, [&] { return run_one(*c, ctx); });
      if (fut.wait_for(std::chrono::seconds(timeout_sec)) != std::future_status::timeout) {
        results.push_back(fut.get());
      } else {
        CheckResult r(c->name());
        r.status = Status::kFail;
        r.message = "timeout";
        results.push_back(r);
        LOG_WARN("[%s] check timeout (%ds)", c->name().c_str(), timeout_sec);
      }
    }
    return results;
  }

  // 并发模式：每个 checker 起一个任务
  std::vector<std::future<CheckResult>> futs;
  futs.reserve(checkers.size());
  for (const auto& c : checkers) {
    futs.emplace_back(std::async(std::launch::async, [&c, &ctx] { return run_one(*c, ctx); }));
  }
  for (size_t i = 0; i < checkers.size(); ++i) {
    if (futs[i].wait_for(std::chrono::seconds(timeout_sec)) != std::future_status::timeout) {
      results.push_back(futs[i].get());
    } else {
      CheckResult r(checkers[i]->name());
      r.status = Status::kFail;
      r.message = "timeout";
      results.push_back(r);
      LOG_WARN("[%s] check timeout (%ds)", checkers[i]->name().c_str(), timeout_sec);
    }
  }
  return results;
}

}  // namespace checker
