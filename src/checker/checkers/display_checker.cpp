/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/display_checker.h"

#include <string>

#include "checker/hw/drm_device.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

namespace qifeng::scm {

CheckResult DisplayChecker::Run(const Context &ctx) {
    CheckResult r(Name());
    auto t0 = GetTimeMs();

    DisplayInfo info = ProbeDisplay(ctx.config.display);

    r.details.emplace_back("device", info.device.empty() ? "none" : info.device);
    r.details.emplace_back("connected", info.connected ? "yes" : "no");
    for (const auto &n : info.notes) {
        r.details.emplace_back("note", n);
    }

    if (info.device.empty()) {
        // 无 DRI 设备节点：在 ARM 上视为告警，x86 上常见，统一 Warning
        r.status = Status::WARNING;
        r.message = "no drm device";
    } else if (info.connected) {
        r.status = Status::PASS;
        r.message = "display connected";
    } else {
        // 有 card 但无 connected connector：未接显示器
        r.status = Status::WARNING;
        r.message = "display not connected";
    }
    r.elapsed_ms = static_cast<int>(GetTimeMs() - t0);
    SLOG_INFO << "[display] " << r.message << " (" << r.elapsed_ms << "ms)";
    return r;
}

}  // namespace qifeng::scm
