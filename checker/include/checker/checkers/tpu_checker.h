// =============================================================================
// tpu_checker.h — TPU 自检
//
// 通过 Sophon SDK 申请设备 0、读写校验设备内存（s2d/d2s 比对），
// 验证 TPU 通路可用。无 SDK 或无设备时返回 Skipped。critical 级别。
// =============================================================================
#pragma once

#include "checker/core/checker.h"

namespace checker {

class TpuChecker : public IChecker {
 public:
  static std::string class_name() { return "tpu"; }
  std::string name() const override { return class_name(); }
  Severity severity() const override { return Severity::kCritical; }
  CheckResult run(const Context& ctx) override;
};

}  // namespace checker
