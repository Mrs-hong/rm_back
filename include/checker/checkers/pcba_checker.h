/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

#include "checker/core/checker.h"

namespace qifeng::scm {

    /**
     * @brief PCBA 硬件自检脚本配置
     * @details 由开发板厂商提供的脚本路径、参数、严重级别、超时及输出解析规则。
     * 与 selftest.json 的 "pcba" 段一一对应。
     */
    struct PcbaConfig {
        std::string exe_command;                       // 脚本绝对路径或可执行文件名
        std::vector<std::string> args;                 // 传给脚本的参数列表
        std::string severity = "warning";              // "critical" 或 "warning"
        int timeout_sec = 60;                          // 脚本执行超时（秒）
        bool parse_output = false;                     // 是否对输出做关键字解析
        std::string pass_keyword = "PASS";             // 判定通过的关键字
        std::string fail_keyword = "FAIL";             // 判定失败的关键字
    };

    /**
     * @brief PCBA 硬件自检脚本检查器
     * @details 调用开发板厂商提供的自检脚本，依据退出码和输出内容判定 PASS/FAIL/SKIPPED。
     * 脚本路径、参数、严重级别、超时时间以及输出关键字均通过 selftest.json:pcba 配置。
     * 严重级别由 json 的 pcba.severity 字段决定（动态），默认 warning。
     */
    class PcbaChecker : public IChecker<PcbaConfig> {
    public:
        static std::string ClassName() { return "pcba"; }
        std::string Name() const override { return ClassName(); }
        enum Severity Severity() const override { return mSeverity; }
        CheckResult Run() override;

    private:
        void ParseConfig(const Json::Value &j, PcbaConfig &cfg) override;

        enum Severity mSeverity{Severity::WARNING};  // 由 ParseConfig 根据 json.severity 更新
    };

}  // namespace qifeng::scm
