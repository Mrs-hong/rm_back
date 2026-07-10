/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <json/json.h>

#include <string>

#include "checker/core/checker.h"

namespace qifeng::scm {

    /**
     * @brief 显示器自检配置
     * @details 与 selftest.json 的 "display" 段一一对应。
     *          DisplayConfig 同时被 hw/drm_device.h 前向声明引用。
     *          支持两种模式：drm（HDMI/DP 显示器）和 serial（迪文串口屏）。
     */
    struct DisplayConfig {
        std::string type = "drm";           // 显示器类型: "drm"(HDMI/DP) 或 "serial"(迪文串口屏)
        std::string dri_path = "/dev/dri";  // DRI 设备目录路径（type=drm 时使用）
        int max_card_index = 8;             // 最大 card 索引（type=drm 时使用）
        std::string device = "";            // 串口设备路径（type=serial 时使用）
        int baud = 115200;                  // 串口波特率（type=serial 时使用）
        int timeout_ms = 500;               // 串口应答超时毫秒（type=serial 时使用）
    };

    /**
     * @brief 显示器自检
     * @details drm 模式：枚举 DRM connector，检查是否有 connected 的显示器。
     * serial 模式：打开串口，发送迪文读版本号指令，检查应答帧。
     * warning 级别（无显示器不阻断启动）。
     */
    class DisplayChecker : public IChecker<DisplayConfig> {
    public:
        static std::string ClassName() { return "display"; }
        std::string Name() const override { return ClassName(); }
        enum Severity Severity() const override { return Severity::WARNING; }
        CheckResult Run() override;

    private:
        void ParseConfig(const Json::Value &j, DisplayConfig &cfg) override;
    };

}  // namespace qifeng::scm
