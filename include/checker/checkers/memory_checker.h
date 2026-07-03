/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "checker/core/checker.h"

namespace qifeng::scm {

/**
 * @brief 内存自检
 * @details 读取 /proc/meminfo 与 sysinfo，校验总内存/可用内存是否满足阈值，
 * 并尝试检测 ECC 信息（若 /sys 提供）。critical 级别。
 */
class MemoryChecker : public IChecker {
public:
    static std::string ClassName() { return "memory"; }
    std::string Name() const override { return ClassName(); }
    enum Severity Severity() const override { return Severity::CRITICAL; }
    CheckResult Run(const Context &ctx) override;
};

}  // namespace qifeng::scm