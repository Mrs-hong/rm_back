// =============================================================================
// memory_checker.h — 内存自检
//
// 读取 /proc/meminfo 与 sysinfo，校验总内存/可用内存是否满足阈值，
// 并尝试检测 ECC 信息（若 /sys 提供）。critical 级别。
// =============================================================================
#pragma once

#include "checker/core/checker.h"

namespace checker {

class MemoryChecker : public IChecker {
 public:
  static std::string class_name() { return "memory"; }
  std::string name() const override { return class_name(); }
  Severity severity() const override { return Severity::kCritical; }
  CheckResult run(const Context& ctx) override;
};

}  // namespace checker
