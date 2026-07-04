/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <json/json.h>

#include <string>

#include "checker/core/checker.h"

namespace qifeng::scm {

    /**
     * @brief 指纹模组自检配置
     * @details 与 selftest.json 的 "fingerprint" 段一一对应
     */
    struct FingerprintConfig {
        std::string device = "/dev/ttyS3";
        int baud = 57600;
        int timeout_ms = 1500;
    };

    /**
     * @brief 指纹模组自检
     * @details 通过串口打开模组设备，配置波特率后发送握手包，限时等待应答。
     * 设备节点缺失或无响应视为 Skipped/Warning。warning 级别。
     */
    class FingerprintChecker : public IChecker<FingerprintConfig> {
    public:
        static std::string ClassName() { return "fingerprint"; }
        std::string Name() const override { return ClassName(); }
        enum Severity Severity() const override { return Severity::WARNING; }
        CheckResult Run() override;

    private:
        void ParseConfig(const Json::Value &j, FingerprintConfig &cfg) override;
    };

}  // namespace qifeng::scm
