/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "checker/core/checker.h"

namespace qifeng::scm {

    /**
     * @brief 磁盘自检
     * @details 对配置的挂载点做 statvfs 容量检查 + 4KB 读写校验，确保分区可读可写
     * 且剩余空间高于阈值。critical 级别。
     */
    class DiskChecker : public IChecker {
    public:
        static std::string ClassName() { return "disk"; }
        std::string Name() const override { return ClassName(); }
        enum Severity Severity() const override { return Severity::CRITICAL; }
        CheckResult Run(const Context &ctx) override;
    };

}  // namespace qifeng::scm