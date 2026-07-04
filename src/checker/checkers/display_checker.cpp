/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checkers/display_checker.h"

#include <chrono>
#include <string>

#include "checker/hw/drm_device.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    void DisplayChecker::ParseConfig(const Json::Value &j, DisplayConfig &cfg) {
        cfg.dri_path = j.isMember("dri_path") && j["dri_path"].isString() ? j["dri_path"].asString()
                                                                          : cfg.dri_path;
        cfg.max_card_index = j.isMember("max_card_index") && j["max_card_index"].isInt()
                                 ? j["max_card_index"].asInt()
                                 : cfg.max_card_index;
    }

    CheckResult DisplayChecker::Run() {
        CheckResult r(Name());
        auto t0 = std::chrono::steady_clock::now();

        DisplayInfo info = ProbeDisplay(mConfig);

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
        r.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - t0)
                                            .count());
        SLOG_INFO << "[display] " << r.message << " (" << r.elapsed_ms << "ms)";
        return r;
    }

}  // namespace qifeng::scm
