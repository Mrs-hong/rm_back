// =============================================================================
// main.cpp — BM1684 自检程序入口
//
// 流程：解析 CLI → 加载配置 → 初始化日志 → 注册检查器 → 并发执行 →
//       汇总 JSON 报告 + 控制台表格 → 退出码(0=OK,1=存在 critical 失败)
// =============================================================================
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "checker/checkers/script_checker.h"
#include "checker/config.h"
#include "checker/core/context.h"
#include "checker/core/registry.h"
#include "checker/core/runner.h"
#include "checker/log/logger.hpp"
#include "checker/util/time_util.h"

namespace checker {

namespace {

using json = nlohmann::json;

// 控制台打印汇总表
void print_table(const std::vector<CheckResult>& results) {
  std::fprintf(stderr, "\n%-18s %-8s %-8s %s\n", "ITEM", "STATUS", "TIME", "MESSAGE");
  std::fprintf(stderr, "%-18s %-8s %-8s %s\n", "----", "------", "----", "-------");
  for (const auto& r : results) {
    std::fprintf(stderr, "%-18s %-8s %-6dms %s\n", r.item.c_str(), status_to_string(r.status),
                 r.elapsed_ms, r.message.c_str());
  }
  std::fprintf(stderr, "\n");
}

// 构建报告 JSON（nlohmann/json）
json build_report(const std::vector<CheckResult>& results, bool overall_ok) {
  json checks = json::object();
  for (const auto& r : results) {
    json detail_obj = json::object();
    for (const auto& kv : r.details) {
      detail_obj[kv.first] = kv.second;
    }
    json item;
    item["status"] = status_to_string(r.status);
    item["elapsed_ms"] = r.elapsed_ms;
    item["message"] = r.message;
    item["details"] = std::move(detail_obj);
    checks[r.item] = std::move(item);
  }

  json root;
  root["timestamp"] = now_iso8601();
  root["version"] = BM1684_SELFTEST_VERSION;
  root["overall"] = overall_ok ? "OK" : "FAIL";
  root["checks"] = std::move(checks);
  return root;
}

}  // namespace

}  // namespace checker

int main(int argc, char** argv) {
  using namespace checker;

  // 1) CLI 与配置路径
  std::string cfg_path = (argc > 1) ? argv[1] : BM1684_DEFAULT_CONFIG_PATH;

  // 2) 先用默认日志器（载入配置前），再按配置重新初始化
  SelfTestConfig config = load_config(cfg_path);
  default_logger().init(config.log_dir, "selftest.log", LogLevel::kInfo);

  LOG_INFO("=== BM1684 SelfTest v%s start ===", BM1684_SELFTEST_VERSION);
  LOG_INFO("config: %s", cfg_path.c_str());

  // 3) 构建上下文
  Context ctx{config, default_logger()};

  // 4) 注册并构建内置检查器
  CheckerRegistry registry;
  register_all(registry);
  auto checkers = registry.build_all();

  // 4b) 追加配置驱动的脚本检查器（每个脚本一项）
  for (const auto& sc : config.scripts) {
    checkers.push_back(std::make_unique<ScriptChecker>(sc));
  }
  LOG_INFO("registered %zu checkers (%zu scripts)", checkers.size(), config.scripts.size());

  // 5) 执行
  Runner runner;
  auto results = runner.run_all(checkers, ctx);

  // 6) 汇总：仅 critical kFail 会导致整体 FAIL
  //    脚本检查器的 severity 由配置决定
  bool overall_ok = true;
  for (const auto& r : results) {
    if (r.status == Status::kFail) {
      // 内置 critical 项 + 脚本项（severity 在 details 中由 ScriptChecker 填充）
      static const std::vector<std::string> critical_items = {"disk", "memory", "network", "tpu",
                                                              "model_inference"};
      bool is_critical = false;
      for (const auto& c : critical_items) {
        if (c == r.item) {
          is_critical = true;
          break;
        }
      }
      // 脚本检查器：检查 details 中的 severity 字段
      if (!is_critical) {
        for (const auto& kv : r.details) {
          if (kv.first == "severity" && kv.second == "critical") {
            is_critical = true;
            break;
          }
        }
      }
      if (is_critical) overall_ok = false;
    }
  }

  // 7) 输出
  print_table(results);
  json report = build_report(results, overall_ok);

  // 写报告文件（失败仅告警，不影响退出码语义）
  std::ofstream out(config.report_path, std::ios::out | std::ios::trunc);
  if (out) {
    out << report.dump(2) << std::endl;
    LOG_INFO("report written to %s", config.report_path.c_str());
  } else {
    LOG_WARN("cannot write report to %s, printing to stdout", config.report_path.c_str());
    std::printf("%s\n", report.dump(2).c_str());
  }

  LOG_INFO("=== SelfTest done: %s ===", overall_ok ? "OK" : "FAIL");
  return overall_ok ? 0 : 1;
}
