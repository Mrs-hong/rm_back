/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "checker/core/checker.h"

namespace qifeng::scm {

/**
 * @brief 网卡自检
 * @details 通过 ioctl SIOCGIFCONF 枚举处于 UP 状态的物理网卡（排除 lo），
 * 再 ping 配置的网关验证连通性。critical 级别。
 */
class NetworkChecker : public IChecker {
public:
    static std::string ClassName() { return "network"; }
    std::string Name() const override { return ClassName(); }
    enum Severity Severity() const override { return Severity::CRITICAL; }
    CheckResult Run(const Context &ctx) override;
};

}  // namespace qifeng::scm