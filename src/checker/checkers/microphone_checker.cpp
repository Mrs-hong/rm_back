/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/microphone_checker.h"

#include <cmath>

#include <chrono>
#include <string>

#include "checker/hw/alsa_capture.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    void MicrophoneChecker::ParseConfig(const Json::Value &j, MicrophoneConfig &cfg) {
        cfg.device = j.isMember("device") && j["device"].isString() ? j["device"].asString() : cfg.device;
        cfg.duration_ms = j.isMember("duration_ms") && j["duration_ms"].isInt() ? j["duration_ms"].asInt()
                                                                                 : cfg.duration_ms;
        cfg.min_rms = j.isMember("min_rms") && j["min_rms"].isInt() ? j["min_rms"].asInt() : cfg.min_rms;
        cfg.sample_rate = j.isMember("sample_rate") && j["sample_rate"].isInt() ? j["sample_rate"].asInt()
                                                                                 : cfg.sample_rate;
        cfg.channels = j.isMember("channels") && j["channels"].isInt() ? j["channels"].asInt() : cfg.channels;
    }

    CheckResult MicrophoneChecker::Run() {
        CheckResult r(Name());
        auto t0 = std::chrono::steady_clock::now();

#if !defined(CHECKER_HAS_ALSA) || !CHECKER_HAS_ALSA
        r.status = Status::SKIPPED;
        r.message = "ALSA not available (build without libasound)";
        r.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - t0)
                                            .count());
        SLOG_INFO << "[microphone] " << r.message;
        return r;
#else
        AlsaCapture cap;
        if (!cap.Open(mConfig.device, static_cast<unsigned>(mConfig.sample_rate),
                      static_cast<unsigned>(mConfig.channels))) {
            r.status = Status::SKIPPED;
            r.message = "pcm open failed: " + mConfig.device;
            r.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - t0)
                                                .count());
            SLOG_INFO << "[microphone] " << r.message;
            return r;
        }
        int frames = mConfig.sample_rate * mConfig.duration_ms / 1000;
        if (frames <= 0) {
            frames = mConfig.sample_rate / 10;  // 默认 100ms
        }
        auto samples = cap.Capture(frames);
        cap.Close();

        if (samples.empty()) {
            r.status = Status::WARNING;
            r.message = "capture failed";
            r.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - t0)
                                                .count());
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

        bool ok = rms >= mConfig.min_rms;
        r.status = ok ? Status::PASS : Status::WARNING;
        r.message = ok ? "microphone ok" : "mic signal too low";
        r.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - t0)
                                            .count());
        SLOG_INFO << "[microphone] " << r.message << " (" << r.elapsed_ms << "ms)";
        return r;
#endif
    }

}  // namespace qifeng::scm
