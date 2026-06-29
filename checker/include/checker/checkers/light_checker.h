// =============================================================================
// light_checker.h — 指示灯自检
//
// 通过 sysfs GPIO 导出引脚、设为输出、翻转电平并回读，校验灯光控制通路。
// x86 无 sysfs gpio 时返回 Skipped。warning 级别。
// =============================================================================
#pragma once

#include "checker/core/checker.h"

namespace checker {

class LightChecker : public IChecker {
 public:
  static std::string class_name() { return "light"; }
  std::string name() const override { return class_name(); }
  Severity severity() const override { return Severity::kWarning; }
  CheckResult run(const Context& ctx) override;
};

}  // namespace checker
