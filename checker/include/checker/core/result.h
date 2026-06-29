// =============================================================================
// result.h — 自检结果的数据模型
//
// 定义自检状态(Status)、严重级别(Severity) 与单项结果(CheckResult)。
// 这是整个工程最基础的数据结构，被 IChecker、Runner 与报告序列化共同使用。
// =============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace checker {

// 自检状态。kSkipped 表示所需资源(设备/库/SDK)不可用，与 kFail 区分：
//   kSkipped = 硬件未装配/库未安装；kFail = 硬件存在但自检异常。
enum class Status {
  kPass,     // 通过
  kFail,     // 失败（硬件存在但异常）
  kWarning,  // 告警（非阻断）
  kSkipped,  // 跳过（资源不可用）
};

// 严重级别：决定该检查器失败是否阻断设备启动(overall=FAIL)。
enum class Severity {
  kCritical,  // 关键：失败则整体 FAIL
  kWarning,   // 告警：失败仅记录，不影响 overall
};

// 单项自检结果
struct CheckResult {
  std::string item;                                   // 检查项名，如 "disk"
  Status status = Status::kSkipped;                   // 最终状态
  int elapsed_ms = 0;                                 // 耗时(毫秒)
  std::string message;                                // 一句话结论
  std::vector<std::pair<std::string, std::string>> details;  // 附加明细 k-v

  // 便捷构造：立即填充 item 与状态
  CheckResult() = default;
  explicit CheckResult(std::string n) : item(std::move(n)) {}
};

// 状态/级别转字符串，用于日志与 JSON 报告
inline const char* status_to_string(Status s) {
  switch (s) {
    case Status::kPass:    return "PASS";
    case Status::kFail:    return "FAIL";
    case Status::kWarning: return "WARN";
    case Status::kSkipped: return "SKIP";
  }
  return "UNKNOWN";
}

inline const char* severity_to_string(Severity sv) {
  return sv == Severity::kCritical ? "critical" : "warning";
}

}  // namespace checker
