// =============================================================================
// script_checker.cpp — 脚本检查器适配器实现
//
// 通过 `timeout` 命令包裹外部 .sh 脚本执行，捕获退出码与 stdout，
// 按脚本输出协议映射为 CheckResult。
// =============================================================================
#include "checker/checkers/script_checker.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

#include "checker/log/logger.hpp"
#include "checker/util/subprocess.h"
#include "checker/util/time_util.h"

namespace checker {

namespace {

using json = nlohmann::json;

// 去除字符串末尾的空白字符
std::string rstrip(const std::string& s) {
  size_t end = s.size();
  while (end > 0 &&
         (s[end - 1] == '\n' || s[end - 1] == '\r' || s[end - 1] == ' ' || s[end - 1] == '\t')) {
    --end;
  }
  return s.substr(0, end);
}

// 尝试从输出末尾提取 JSON 行；成功则返回 json 对象并从 output 中移除该行
bool try_extract_json(std::string& output, json* out) {
  // 找最后一个非空行
  size_t end = output.size();
  while (end > 0 && (output[end - 1] == '\n' || output[end - 1] == '\r' || output[end - 1] == ' ' ||
                     output[end - 1] == '\t')) {
    --end;
  }
  if (end == 0) return false;
  size_t start = output.rfind('\n', end - 1);
  start = (start == std::string::npos) ? 0 : start + 1;
  std::string last_line = output.substr(start, end - start);

  // 尝试解析为 JSON
  try {
    *out = json::parse(last_line);
    if (!out->is_object()) return false;
    // 从 output 中移除该行
    output.resize(start);
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

ScriptChecker::ScriptChecker(const ScriptConfig& cfg) : cfg_(cfg) {}

std::string ScriptChecker::name() const { return cfg_.name; }

Severity ScriptChecker::severity() const {
  return (cfg_.severity == "critical") ? Severity::kCritical : Severity::kWarning;
}

CheckResult ScriptChecker::run(const Context& /*ctx*/) {
  CheckResult r(name());
  Timer timer;

  // 在 details 中记录 severity，供 main.cpp 判定 overall_ok
  r.details.emplace_back("severity", cfg_.severity);
  r.details.emplace_back("script", cfg_.path);

  // 检查脚本文件是否存在
  struct stat st;
  if (::stat(cfg_.path.c_str(), &st) != 0) {
    r.status = Status::kSkipped;
    r.message = "script not found: " + cfg_.path;
    r.elapsed_ms = timer.elapsed_ms();
    LOG_INFO("[%s] %s", cfg_.name.c_str(), r.message.c_str());
    return r;
  }

  // 用 timeout 命令包裹脚本，防卡死
  // 2>&1 合并 stderr 到 stdout 便于诊断
  char cmd[4096];
  std::snprintf(cmd, sizeof(cmd), "timeout %d bash %s 2>&1", cfg_.timeout_sec, cfg_.path.c_str());
  auto pr = run_subprocess(cmd);

  // 解析退出码
  int code = pr.exit_code;
  r.details.emplace_back("exit_code", std::to_string(code));

  // 尝试从输出末尾提取 JSON 协议行
  std::string output = pr.out;
  json meta;
  bool has_meta = try_extract_json(output, &meta);

  // 提取 message
  std::string message;
  if (has_meta && meta.contains("message") && meta["message"].is_string()) {
    message = meta["message"].get<std::string>();
  } else {
    message = rstrip(output);
    if (message.empty()) message = "script completed";
    // 截断过长的 message
    if (message.size() > 200) message = message.substr(0, 200) + "...";
  }

  // 提取 details（字符串直接取值，其他类型用 JSON dump）
  if (has_meta && meta.contains("details") && meta["details"].is_object()) {
    for (auto it = meta["details"].begin(); it != meta["details"].end(); ++it) {
      if (it.value().is_string()) {
        r.details.emplace_back(it.key(), it.value().get<std::string>());
      } else {
        r.details.emplace_back(it.key(), it.value().dump());
      }
    }
  }

  // 退出码 → Status 映射
  switch (code) {
    case 0:
      r.status = Status::kPass;
      break;
    case 1:
      r.status = Status::kFail;
      break;
    case 2:
      r.status = Status::kSkipped;
      break;
    case 3:
      r.status = Status::kWarning;
      break;
    case 124:
      // timeout 命令超时返回 124
      r.status = Status::kFail;
      message = "script timeout (" + std::to_string(cfg_.timeout_sec) + "s)";
      break;
    case 126:
    case 127:
      // 126=不可执行, 127=命令未找到
      r.status = Status::kSkipped;
      message = "script not executable (exit " + std::to_string(code) + ")";
      break;
    default:
      // 其他非零退出码视为失败
      r.status = Status::kFail;
      break;
  }

  r.message = message;
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[%s] %s (%dms)", cfg_.name.c_str(), r.message.c_str(), r.elapsed_ms);
  return r;
}

}  // namespace checker
