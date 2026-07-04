/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <json/json.h>

#include <string>
#include <vector>

#include "checker/core/checker.h"

namespace qifeng::scm {

    /**
     * @brief 磁盘自检配置
     * @details 与 selftest.json 的 "disk" 段一一对应
     */
    struct DiskConfig {
        std::vector<std::string> mounts{"/", "/data", "/opt/sophon"};
        int min_free_pct = 5;
    };

    /**
     * @brief 磁盘自检
     * @details 对配置的挂载点做 statvfs 容量检查 + 4KB 读写校验，确保分区可读可写
     *          且剩余空间高于阈值。critical 级别。
     *          自包含配置结构 + 自解析 JSON，新增检查项零改动 core 层。
     */
    class DiskChecker : public IChecker<DiskConfig> {
    public:
        static std::string ClassName() { return "disk"; }
        std::string Name() const override { return ClassName(); }
        enum Severity Severity() const override { return Severity::CRITICAL; }
        CheckResult Run() override;

    private:
        void ParseConfig(const Json::Value &j, DiskConfig &cfg) override;
    };

}  // namespace qifeng::scm
