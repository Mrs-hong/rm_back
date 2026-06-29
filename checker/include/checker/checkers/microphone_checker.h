// =============================================================================
// microphone_checker.h — 麦克风自检
//
// 有 ALSA：以 16kHz 单声道录音若干毫秒，计算 RMS 能量判断是否有信号。
// 无 ALSA：返回 Skipped。warning 级别。
// =============================================================================
#pragma once

#include "checker/core/checker.h"

namespace checker {

class MicrophoneChecker : public IChecker {
 public:
  static std::string class_name() { return "microphone"; }
  std::string name() const override { return class_name(); }
  Severity severity() const override { return Severity::kWarning; }
  CheckResult run(const Context& ctx) override;
};

}  // namespace checker
