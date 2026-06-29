// =============================================================================
// network_checker.h — 网卡自检
//
// 通过 ioctl SIOCGIFCONF 枚举处于 UP 状态的物理网卡（排除 lo），
// 再 ping 配置的网关验证连通性。critical 级别。
// =============================================================================
#pragma once

#include "checker/core/checker.h"

namespace checker {

class NetworkChecker : public IChecker {
 public:
  static std::string class_name() { return "network"; }
  std::string name() const override { return class_name(); }
  Severity severity() const override { return Severity::kCritical; }
  CheckResult run(const Context& ctx) override;
};

}  // namespace checker
