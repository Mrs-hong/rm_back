// =============================================================================
// model_inference_checker.h — 小模型推理自检
//
// 加载配置中指定的 probe.bmodel，用零张量跑一次推理，验证 TPU + 模型
// 加载 + 推理链路完整。无 SDK/无设备/无模型文件时返回 Skipped。critical 级别。
// =============================================================================
#pragma once

#include "checker/core/checker.h"

namespace checker {

class ModelInferenceChecker : public IChecker {
 public:
  static std::string class_name() { return "model_inference"; }
  std::string name() const override { return class_name(); }
  Severity severity() const override { return Severity::kCritical; }
  CheckResult run(const Context& ctx) override;
};

}  // namespace checker
