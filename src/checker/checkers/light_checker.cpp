/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/light_checker.h"

#include <chrono>
#include <thread>

#include "checker/hw/gpio.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    void LightChecker::ParseConfig(const Json::Value &j, LightConfig &cfg) {
        cfg.gpio = j.isMember("gpio") && j["gpio"].isString() ? j["gpio"].asString() : cfg.gpio;
    }

    CheckResult LightChecker::Run() {
        CheckResult r(Name());
        auto t0 = std::chrono::steady_clock::now();

        Gpio gpio(mConfig.gpio);
        if (!gpio.IsOpen()) {
            // 无 libgpiod 或引脚不可用：跳过
            r.status = Status::SKIPPED;
            r.message = "gpio " + mConfig.gpio + " not available";
            r.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - t0)
                                                .count());
            SLOG_INFO << "[light] " << r.message;
            return r;
        }

        // 翻转电平并回读
        bool ok = gpio.Set(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        ok &= gpio.Set(false);
        bool high = false;
        bool rd = gpio.Get(high);
        r.details.emplace_back("readback", rd ? (high ? "1" : "0") : "fail");

        r.status = ok ? Status::PASS : Status::WARNING;
        r.message = ok ? "light control ok" : "light control failed";
        r.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - t0)
                                            .count());
        SLOG_INFO << "[light] " << r.message << " (" << r.elapsed_ms << "ms)";
        return r;
    }

}  // namespace qifeng::scm
