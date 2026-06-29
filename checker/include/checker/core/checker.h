// =============================================================================
// checker.h — 自检器统一接口
//
// 所有具体检查器(DiskChecker 等)实现 IChecker，由 Registry 注册、Runner 调度。
// 设计要点：
//   - run() 接收 const Context&，避免全局状态，便于测试与并发。
//   - 资源不可用时返回 Status::kSkipped（与 kFail 区分）。
// =============================================================================
#pragma once

#include <memory>
#include <string>

#include "checker/core/context.h"
#include "checker/core/result.h"

namespace checker {

class IChecker {
 public:
  virtual ~IChecker() = default;

  // 检查项名，如 "disk"（用作报告 key，需唯一）
  virtual std::string name() const = 0;

  // 严重级别：critical 失败会令 overall=FAIL
  virtual Severity severity() const = 0;

  // 实际检查逻辑。Runner 负责超时包裹，本函数应尽量快速且 RAII 释放资源。
  virtual CheckResult run(const Context& ctx) = 0;
};

using CheckerPtr = std::unique_ptr<IChecker>;

}  // namespace checker
