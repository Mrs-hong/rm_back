/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <json/json.h>

#include <string>

#include "checker/core/checker.h"

namespace qifeng::scm {

    /**
     * @brief 网卡自检配置
     * @details 与 selftest.json 的 "network" 段一一对应
     */
    struct NetworkConfig {
        std::string gateway = "192.168.1.1";
        int ping_count = 3;
        int ping_timeout_sec = 1;  // ping 单次超时（秒）
    };

    /**
     * @brief 网卡自检
     * @details 通过 ioctl SIOCGIFCONF 枚举处于 UP 状态的物理网卡（排除 lo），
     * 再 ping 配置的网关验证连通性。critical 级别。
     */
    class NetworkChecker : public IChecker<NetworkConfig> {
    public:
        static std::string ClassName() { return "network"; }
        std::string Name() const override { return ClassName(); }
        enum Severity Severity() const override { return Severity::CRITICAL; }
        CheckResult Run() override;

    private:
        void ParseConfig(const Json::Value &j, NetworkConfig &cfg) override;
    };

}  // namespace qifeng::scm
