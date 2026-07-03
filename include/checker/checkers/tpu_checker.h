/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "checker/core/checker.h"

namespace qifeng::scm {

/**
 * @brief TPU 自检
 * @details 通过 Sophon SDK 申请设备 0、读写校验设备内存（s2d/d2s 比对），
 * 验证 TPU 通路可用。无 SDK 或无设备时返回 Skipped。critical 级别。
 */
class TpuChecker : public IChecker {
public:
    static std::string ClassName() { return "tpu"; }
    std::string Name() const override { return ClassName(); }
    enum Severity Severity() const override { return Severity::CRITICAL; }
    CheckResult Run(const Context &ctx) override;
};

}  // namespace qifeng::scm