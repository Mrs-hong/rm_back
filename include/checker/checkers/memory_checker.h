/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <json/json.h>

#include "checker/core/checker.h"

namespace qifeng::scm {

    /**
     * @brief 内存自检配置
     * @details 与 selftest.json 的 "memory" 段一一对应
     */
    struct MemoryConfig {
        int min_available_mb = 128;
    };

    /**
     * @brief 内存自检
     * @details 读取 /proc/meminfo 获取可用内存，对比阈值。critical 级别。
     *          自包含配置结构 + 自解析 JSON，新增检查项零改动 core 层。
     */
    class MemoryChecker : public IChecker<MemoryConfig> {
    public:
        static std::string ClassName() { return "memory"; }
        std::string Name() const override { return ClassName(); }
        enum Severity Severity() const override { return Severity::CRITICAL; }
        CheckResult Run() override;

    private:
        void ParseConfig(const Json::Value &j, MemoryConfig &cfg) override;
    };

}  // namespace qifeng::scm
