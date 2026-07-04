/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <json/json.h>

#include <string>

#include "checker/core/checker.h"

namespace qifeng::scm {

    /**
     * @brief 麦克风自检配置
     * @details 与 selftest.json 的 "microphone" 段一一对应
     */
    struct MicrophoneConfig {
        std::string device = "default";
        int duration_ms = 400;
        int min_rms = 50;
        int sample_rate = 16000;  // 采样率（Hz）
        int channels = 1;         // 通道数
    };

    /**
     * @brief 麦克风自检
     * @details 有 ALSA：以配置的采样率/声道录音若干毫秒，计算 RMS 能量判断是否有信号。
     * 无 ALSA：返回 Skipped。warning 级别。
     */
    class MicrophoneChecker : public IChecker<MicrophoneConfig> {
    public:
        static std::string ClassName() { return "microphone"; }
        std::string Name() const override { return ClassName(); }
        enum Severity Severity() const override { return Severity::WARNING; }
        CheckResult Run() override;

    private:
        void ParseConfig(const Json::Value &j, MicrophoneConfig &cfg) override;
    };

}  // namespace qifeng::scm
