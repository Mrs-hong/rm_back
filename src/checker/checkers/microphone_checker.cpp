/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/microphone_checker.h"

#include <cmath>

#include "checker/hw/alsa_capture.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

namespace qifeng::scm {

CheckResult MicrophoneChecker::Run(const Context &ctx) {
    CheckResult r(Name());
    auto t0 = GetTimeMs();

#if !defined(CHECKER_HAS_ALSA) || !CHECKER_HAS_ALSA
    (void)ctx;
    r.status = Status::SKIPPED;
    r.message = "ALSA not available (build without libasound)";
    r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
    SLOG_INFO << "[microphone] " << r.message;
    return r;
#else
    const auto &cfg = ctx.config.microphone;
    AlsaCapture cap;
    if (!cap.Open(cfg.device, static_cast<unsigned>(cfg.sample_rate), static_cast<unsigned>(cfg.channels))) {
        r.status = Status::SKIPPED;
        r.message = "pcm open failed: " + cfg.device;
        r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
        SLOG_INFO << "[microphone] " << r.message;
        return r;
    }
    int frames = cfg.sample_rate * (cfg.duration_ms) / 1000;
    if (frames <= 0) {
        frames = cfg.sample_rate / 10;  // 默认 100ms
    }
    auto samples = cap.Capture(frames);
    cap.Close();

    if (samples.empty()) {
        r.status = Status::WARNING;
        r.message = "capture failed";
        r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
        return r;
    }

    // 计算 RMS 能量
    long long sum = 0;
    for (int16_t s : samples) {
        sum += (long long)s * s;
    }
    double rms = std::sqrt(static_cast<double>(sum) / static_cast<double>(samples.size()));
    std::string db = std::to_string(20.0 * std::log10(rms + 1.0)).substr(0, 6) + " dB";
    r.details.emplace_back("rms", std::to_string((int)rms));
    r.details.emplace_back("db", db);

    bool ok = rms >= cfg.min_rms;
    r.status = ok ? Status::PASS : Status::WARNING;
    r.message = ok ? "microphone ok" : "mic signal too low";
    r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
    SLOG_INFO << "[microphone] " << r.message << " (" << r.elapsed_ms << "ms)";
    return r;
#endif
}

}  // namespace qifeng::scm