// =============================================================================
// light_checker.cpp — 指示灯自检（sysfs GPIO）
// =============================================================================
#include "checker/checkers/light_checker.h"

#include <thread>

#include "checker/hw/gpio.h"
#include "checker/log/logger.hpp"
#include "checker/util/time_util.h"

namespace checker {

CheckResult LightChecker::run(const Context& ctx) {
  CheckResult r(name());
  Timer timer;

  Gpio gpio(ctx.config.light.gpio);
  if (!gpio.is_open()) {
    // 无 sysfs gpio（x86 常见）：跳过
    r.status = Status::kSkipped;
    r.message = "gpio " + ctx.config.light.gpio + " not available";
    r.elapsed_ms = timer.elapsed_ms();
    LOG_INFO("[light] %s", r.message.c_str());
    return r;
  }

  // 翻转电平并回读
  bool ok = gpio.set(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  ok &= gpio.set(false);
  bool high = false;
  bool rd = gpio.get(high);
  r.details.emplace_back("readback", rd ? (high ? "1" : "0") : "fail");

  r.status = ok ? Status::kPass : Status::kWarning;
  r.message = ok ? "light control ok" : "light control failed";
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[light] %s (%dms)", r.message.c_str(), r.elapsed_ms);
  return r;
}

}  // namespace checker
