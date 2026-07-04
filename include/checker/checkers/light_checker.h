/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <json/json.h>

#include <string>

#include "checker/core/checker.h"

namespace qifeng::scm {

    /**
     * @brief 指示灯自检配置
     * @details 与 selftest.json 的 "light" 段一一对应
     */
    struct LightConfig {
        std::string gpio = "488";
    };

    /**
     * @brief 指示灯自检
     * @details 通过 libgpiod 控制引脚、设为输出、翻转电平并回读，校验灯光控制通路。
     * 无 libgpiod 或引脚不可用时返回 Skipped。warning 级别。
     */
    class LightChecker : public IChecker<LightConfig> {
    public:
        static std::string ClassName() { return "light"; }
        std::string Name() const override { return ClassName(); }
        enum Severity Severity() const override { return Severity::WARNING; }
        CheckResult Run() override;

    private:
        void ParseConfig(const Json::Value &j, LightConfig &cfg) override;
    };

}  // namespace qifeng::scm
