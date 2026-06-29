// =============================================================================
// microphone_checker.cpp — 麦克风自检
// =============================================================================
#include "checker/checkers/microphone_checker.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "checker/hw/alsa_capture.h"
#include "checker/log/logger.hpp"
#include "checker/util/time_util.h"

namespace checker {

CheckResult MicrophoneChecker::run(const Context& ctx) {
  CheckResult r(name());
  Timer timer;

#if !defined(CHECKER_HAS_ALSA) || !CHECKER_HAS_ALSA
  (void)ctx;
  r.status = Status::kSkipped;
  r.message = "ALSA not available (build without libasound)";
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[microphone] %s", r.message.c_str());
  return r;
#else
  const auto& cfg = ctx.config.microphone;
  AlsaCapture cap;
  if (!cap.open(cfg.device, 16000, 1)) {
    r.status = Status::kSkipped;
    r.message = "pcm open failed: " + cfg.device;
    r.elapsed_ms = timer.elapsed_ms();
    LOG_INFO("[microphone] %s", r.message.c_str());
    return r;
  }
  int frames = 16000 * (cfg.duration_ms) / 1000;
  if (frames <= 0) frames = 1600;
  auto samples = cap.capture(frames);
  cap.close();

  if (samples.empty()) {
    r.status = Status::kWarning;
    r.message = "capture failed";
    r.elapsed_ms = timer.elapsed_ms();
    return r;
  }

  // 计算 RMS 能量
  long long sum = 0;
  for (int16_t s : samples) sum += (long long)s * s;
  double rms = std::sqrt((double)sum / samples.size());
  char db[32];
  snprintf(db, sizeof(db), "%.1f dB", 20.0 * std::log10(rms + 1.0));
  r.details.emplace_back("rms", std::to_string((int)rms));
  r.details.emplace_back("db", db);

  bool ok = rms >= cfg.min_rms;
  r.status = ok ? Status::kPass : Status::kWarning;
  r.message = ok ? "microphone ok" : "mic signal too low";
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[microphone] %s (%dms)", r.message.c_str(), r.elapsed_ms);
  return r;
#endif
}

}  // namespace checker
