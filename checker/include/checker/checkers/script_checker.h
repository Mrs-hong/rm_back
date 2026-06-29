// =============================================================================
// script_checker.h — 脚本检查器适配器
//
// 将部门同事提供的外部 .sh 监测脚本包装为 IChecker，纳入统一框架：
//   - 并发调度（Runner）、超时保护（timeout 命令包裹）
//   - 结构化 JSON 报告、severity 分级
//   - 与原生 C++ 检查器共存
//
// 脚本输出协议（契约）：
//   退出码：
//     0 = pass        1 = fail
//     2 = skipped     3 = warning
//     124 = 超时(fail) 126/127 = 不可执行(skipped)
//   stdout（可选）：最后一行为 JSON 对象，格式：
//     {"message":"简要描述","details":{"key":"value",...}}
//   若无 JSON 行，则取 stdout 末尾文本作为 message，details 为空。
//
// 配置示例（selftest.json）：
//   "scripts": [
//     {"name":"fingerprint","path":"/etc/bm1684-selftest/scripts/check_fp.sh",
//      "severity":"warning","timeout_sec":5}
//   ]
// =============================================================================
#pragma once

#include <string>

#include "checker/core/checker.h"
#include "checker/core/context.h"

namespace checker {

class ScriptChecker : public IChecker {
 public:
  explicit ScriptChecker(const ScriptConfig& cfg);

  std::string name() const override;
  Severity severity() const override;
  CheckResult run(const Context& ctx) override;

 private:
  ScriptConfig cfg_;
};

}  // namespace checker
