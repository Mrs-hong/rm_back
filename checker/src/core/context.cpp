// =============================================================================
// context.cpp — 配置加载（基于 nlohmann/json）
//
// 从 selftest.json 读取配置填充 SelfTestConfig；文件缺失或解析失败时
// 返回内置默认并告警。
// =============================================================================
#include "checker/core/context.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace checker {

namespace {

using json = nlohmann::json;

// 安全取值辅助：键不存在或类型不符时返回默认值
template <class T>
T get_or(const json& j, const char* key, const T& def) {
  if (j.contains(key) && !j[key].is_null()) {
    try {
      return j[key].get<T>();
    } catch (...) {
      return def;
    }
  }
  return def;
}

// 从 JSON 对象提取各子段配置
void apply_config(const json& root, SelfTestConfig& cfg) {
  cfg.per_item_timeout_sec = get_or(root, "per_item_timeout_sec", cfg.per_item_timeout_sec);
  cfg.parallel = get_or(root, "parallel", cfg.parallel);
  cfg.report_path = get_or(root, "report_path", cfg.report_path);
  cfg.log_dir = get_or(root, "log_dir", cfg.log_dir);

  if (root.contains("disk")) {
    const auto& d = root["disk"];
    cfg.disk.min_free_pct = get_or(d, "min_free_pct", cfg.disk.min_free_pct);
    if (d.contains("mounts") && d["mounts"].is_array()) {
      cfg.disk.mounts.clear();
      for (const auto& v : d["mounts"]) {
        if (v.is_string()) cfg.disk.mounts.push_back(v.get<std::string>());
      }
    }
  }
  if (root.contains("memory")) {
    cfg.memory.min_available_mb =
        get_or(root["memory"], "min_available_mb", cfg.memory.min_available_mb);
  }
  if (root.contains("fingerprint")) {
    const auto& f = root["fingerprint"];
    cfg.fingerprint.device = get_or(f, "device", cfg.fingerprint.device);
    cfg.fingerprint.baud = get_or(f, "baud", cfg.fingerprint.baud);
    cfg.fingerprint.timeout_ms = get_or(f, "timeout_ms", cfg.fingerprint.timeout_ms);
  }
  if (root.contains("microphone")) {
    const auto& m = root["microphone"];
    cfg.microphone.device = get_or(m, "device", cfg.microphone.device);
    cfg.microphone.duration_ms = get_or(m, "duration_ms", cfg.microphone.duration_ms);
    cfg.microphone.min_rms = get_or(m, "min_rms", cfg.microphone.min_rms);
  }
  if (root.contains("light")) {
    cfg.light.gpio = get_or(root["light"], "gpio", cfg.light.gpio);
  }
  if (root.contains("network")) {
    const auto& n = root["network"];
    cfg.network.gateway = get_or(n, "gateway", cfg.network.gateway);
    cfg.network.ping_count = get_or(n, "ping_count", cfg.network.ping_count);
  }
  if (root.contains("model")) {
    cfg.model.path = get_or(root["model"], "path", cfg.model.path);
  }
  // 脚本检查器列表
  if (root.contains("scripts") && root["scripts"].is_array()) {
    for (const auto& s : root["scripts"]) {
      ScriptConfig sc;
      sc.name = get_or(s, "name", std::string(""));
      sc.path = get_or(s, "path", std::string(""));
      sc.severity = get_or(s, "severity", std::string("warning"));
      sc.timeout_sec = get_or(s, "timeout_sec", 5);
      if (!sc.name.empty() && !sc.path.empty()) {
        cfg.scripts.push_back(std::move(sc));
      }
    }
  }
}

}  // namespace

SelfTestConfig load_config(const std::string& path) {
  SelfTestConfig cfg;  // 内置默认
  std::ifstream ifs(path);
  if (!ifs) {
    LOG_WARN("config file not found: %s, using built-in defaults", path.c_str());
    return cfg;
  }
  std::stringstream ss;
  ss << ifs.rdbuf();
  try {
    json root = json::parse(ss.str());
    apply_config(root, cfg);
    LOG_INFO("config loaded from %s", path.c_str());
  } catch (const std::exception& e) {
    LOG_WARN("config parse failed: %s, using built-in defaults", e.what());
  }
  return cfg;
}

}  // namespace checker
