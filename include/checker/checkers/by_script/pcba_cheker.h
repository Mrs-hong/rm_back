/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "checker/core/checker.h"

namespace qifeng::scm {

/**
 * @brief PCBA 硬件自检脚本检查器
 * @details 调用开发板厂商提供的自检脚本，依据退出码和输出内容判定 PASS/FAIL/SKIPPED。
 * 脚本路径、参数、严重级别、超时时间以及输出关键字均通过 selftest.json:pcba 配置。
 */
class PcbaChecker : public IChecker {
public:
    /**
     * @brief 检查器类名，用于注册表识别
     */
    static std::string ClassName() { return "PcbaChecker"; }

    /**
     * @brief 检查项名，报告 key 为 "pcba"
     */
    std::string Name() const override { return "pcba"; }

    /**
     * @brief 严重级别，由 selftest.json:pcba.severity 决定
     */
    enum Severity Severity() const override;

    /**
     * @brief 执行 PCBA 自检脚本并返回结果
     */
    CheckResult Run(const Context &ctx) override;

private:
    enum Severity mSeverity = Severity::WARNING;  // 由 selftest.json:pcba.severity 在 Run() 中更新
};

}  // namespace qifeng::scm
