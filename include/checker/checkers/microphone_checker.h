/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "checker/core/checker.h"

namespace qifeng::scm {

/**
 * @brief 麦克风自检
 * @details 有 ALSA：以 16kHz 单声道录音若干毫秒，计算 RMS 能量判断是否有信号。
 * 无 ALSA：返回 Skipped。warning 级别。
 */
class MicrophoneChecker : public IChecker {
public:
    static std::string ClassName() { return "microphone"; }
    std::string Name() const override { return ClassName(); }
    enum Severity Severity() const override { return Severity::WARNING; }
    CheckResult Run(const Context &ctx) override;
};

}  // namespace qifeng::scm