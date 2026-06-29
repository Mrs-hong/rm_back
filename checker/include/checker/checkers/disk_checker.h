// =============================================================================
// disk_checker.h — 磁盘自检
//
// 对配置的挂载点做 statvfs 容量检查 + 4KB 读写校验，确保分区可读可写
// 且剩余空间高于阈值。critical 级别。
// =============================================================================
#pragma once

#include "checker/core/checker.h"

namespace checker {

class DiskChecker : public IChecker {
 public:
  static std::string class_name() { return "disk"; }
  std::string name() const override { return class_name(); }
  Severity severity() const override { return Severity::kCritical; }
  CheckResult run(const Context& ctx) override;
};

}  // namespace checker
