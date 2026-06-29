// =============================================================================
// display_checker.cpp — 显示器自检
// =============================================================================
#include "checker/checkers/display_checker.h"

#include <string>

#include "checker/hw/drm_device.h"
#include "checker/log/logger.hpp"
#include "checker/util/time_util.h"

namespace checker {

CheckResult DisplayChecker::run(const Context& ctx) {
  (void)ctx;
  CheckResult r(name());
  Timer timer;

  DisplayInfo info = probe_display();

  r.details.emplace_back("device", info.device.empty() ? "none" : info.device);
  r.details.emplace_back("connected", info.connected ? "yes" : "no");
  for (const auto& n : info.notes) r.details.emplace_back("note", n);

  if (info.device.empty()) {
    // 无 DRI 设备节点：在 ARM 上视为告警，x86 上常见，统一 Warning
    r.status = Status::kWarning;
    r.message = "no drm device";
  } else if (info.connected) {
    r.status = Status::kPass;
    r.message = "display connected";
  } else {
    // 有 card 但无 connected connector：未接显示器
    r.status = Status::kWarning;
    r.message = "display not connected";
  }
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[display] %s (%dms)", r.message.c_str(), r.elapsed_ms);
  return r;
}

}  // namespace checker
