/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "checker/core/checker.h"

namespace qifeng::scm {

/**
 * @brief 风扇与散热自检
 * @details 通过 Sophon SDK 读取芯片温度、板卡温度和风扇转速，
 * 验证散热系统工作正常。无 SDK 或无设备时返回 Skipped。
 * 检查逻辑：
 *   1. 读取 chip_temp / board_temp，若超过 max_temp_threshold 则 FAIL
 *   2. 读取 fan_speed（RPM），若为 0 且温度 > 0 则 WARNING（风扇可能停转）
 *   3. 温度和转速均正常则 PASS
 */
class FanChecker : public IChecker {
public:
    static std::string ClassName() { return "fan"; }
    std::string Name() const override { return ClassName(); }
    enum Severity Severity() const override { return Severity::CRITICAL; }
    CheckResult Run(const Context &ctx) override;
};

}  // namespace qifeng::scm
