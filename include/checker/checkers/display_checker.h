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
     */
    struct DisplayConfig {
        std::string dri_path = "/dev/dri";  // DRI 设备目录路径
        int max_card_index = 8;             // 最大 card 索引（扫描 card0 ~ N-1）
    };

    /**
     * @brief 显示器自检
     * @details 有 libdrm：枚举 DRM connector，检查是否有 connected 的显示器。
     * 无 libdrm：降级为 /dev/dri/card* 与 /sys/class/drm 节点存在性检查。
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
