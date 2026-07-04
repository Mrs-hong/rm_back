/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <json/json.h>

#include <string>

#include "checker/core/checker.h"

namespace qifeng::scm {

    /**
     * @brief 小模型推理自检配置
     * @details 与 selftest.json 的 "model_inference" 段一一对应
     */
    struct ModelInferenceConfig {
        std::string path = "/opt/sophon/selftest/fsmn_fp32_.bmodel";
    };

    /**
     * @brief 小模型推理自检
     * @details 加载配置中指定的 probe.bmodel，用零张量跑一次推理，验证 TPU + 模型
     * 加载 + 推理链路完整。无 SDK/无设备/无模型文件时返回 Skipped。critical 级别。
     */
    class ModelInferenceChecker : public IChecker<ModelInferenceConfig> {
    public:
        static std::string ClassName() { return "model_inference"; }
        std::string Name() const override { return ClassName(); }
        enum Severity Severity() const override { return Severity::CRITICAL; }
        CheckResult Run() override;

    private:
        void ParseConfig(const Json::Value &j, ModelInferenceConfig &cfg) override;
    };

}  // namespace qifeng::scm
