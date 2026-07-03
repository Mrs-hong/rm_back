/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/light_checker.h"

#include <thread>

#include "checker/hw/gpio.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

namespace qifeng::scm {

CheckResult LightChecker::Run(const Context &ctx) {
    CheckResult r(Name());
    auto t0 = GetTimeMs();

    Gpio gpio(ctx.config.light.gpio);
    if (!gpio.IsOpen()) {
        // 无 libgpiod 或引脚不可用：跳过
        r.status = Status::SKIPPED;
        r.message = "gpio " + ctx.config.light.gpio + " not available";
        r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
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
    r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
    SLOG_INFO << "[light] " << r.message << " (" << r.elapsed_ms << "ms)";
    return r;
}

}  // namespace qifeng::scm