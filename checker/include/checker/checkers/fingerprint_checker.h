// =============================================================================
// fingerprint_checker.h — 指纹模组自检
//
// 通过串口打开模组设备，配置波特率后发送握手包，限时等待应答。
// 设备节点缺失或无响应视为 Skipped/Warning。warning 级别。
// =============================================================================
#pragma once

#include "checker/core/checker.h"

namespace checker {

class FingerprintChecker : public IChecker {
 public:
  static std::string class_name() { return "fingerprint"; }
  std::string name() const override { return class_name(); }
  Severity severity() const override { return Severity::kWarning; }
  CheckResult run(const Context& ctx) override;
};

}  // namespace checker
