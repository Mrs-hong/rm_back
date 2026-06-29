// =============================================================================
// display_checker.h — 显示器自检
//
// 有 libdrm：枚举 DRM connector，检查是否有 connected 的显示器。
// 无 libdrm：降级为 /dev/dri/card* 与 /sys/class/drm 节点存在性检查。
// warning 级别（无显示器不阻断启动）。
// =============================================================================
#pragma once

#include "checker/core/checker.h"

namespace checker {

class DisplayChecker : public IChecker {
 public:
  static std::string class_name() { return "display"; }
  std::string name() const override { return class_name(); }
  Severity severity() const override { return Severity::kWarning; }
  CheckResult run(const Context& ctx) override;
};

}  // namespace checker
