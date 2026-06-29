// =============================================================================
// context.h — 运行期配置与执行上下文
//
// SelfTestConfig 由 selftest.json 解析得到；Context 聚合配置、日志器与平台
// 标志，以 const 引用注入各 IChecker，消除全局可变状态。
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "checker/log/logger.hpp"
#include "checker/config.h"  // CMake 生成的 CHECKER_HAS_* 宏

namespace checker {

// 各检查器的配置子段（与 config/selftest.json 一一对应）
struct DiskConfig {
  std::vector<std::string> mounts{"/", "/data", "/opt/sophon"};
  int min_free_pct = 5;
};
struct MemoryConfig {
  int min_available_mb = 128;
};
struct FingerprintConfig {
  std::string device = "/dev/ttyS3";
  int baud = 57600;
  int timeout_ms = 1500;
};
struct MicrophoneConfig {
  std::string device = "default";
  int duration_ms = 400;
  int min_rms = 50;
};
struct LightConfig {
  std::string gpio = "488";
};
struct NetworkConfig {
  std::string gateway = "192.168.1.1";
  int ping_count = 3;
};
struct ModelConfig {
  std::string path = "/opt/sophon/selftest/probe.bmodel";
};

// 脚本检查器配置：将外部 .sh 脚本包装为 IChecker
struct ScriptConfig {
  std::string name;           // 检查项名（报告 key，需唯一）
  std::string path;           // 脚本绝对路径
  std::string severity = "warning";  // "critical" 或 "warning"
  int timeout_sec = 5;        // 执行超时
};

// 顶层配置
struct SelfTestConfig {
  int per_item_timeout_sec = 5;
  bool parallel = true;
  std::string report_path = "/var/log/bm1684-selftest/report.json";
  std::string log_dir = "/var/log/bm1684-selftest";

  DiskConfig disk;
  MemoryConfig memory;
  FingerprintConfig fingerprint;
  MicrophoneConfig microphone;
  LightConfig light;
  NetworkConfig network;
  ModelConfig model;
  std::vector<ScriptConfig> scripts;  // 配置驱动的脚本检查器列表
};

// 执行上下文：注入到每个 checker 的 run() 中
struct Context {
  const SelfTestConfig& config;
  Logger& log;

  // 平台能力标志（来自编译期 + 运行期探测），决定 checker 行为
  bool has_bm_sdk = (CHECKER_HAS_BM_SDK != 0);
  bool has_alsa = (CHECKER_HAS_ALSA != 0);
  bool has_drm = (CHECKER_HAS_DRM != 0);
};

// 从 JSON 文件加载配置；文件不存在或解析失败时返回内置默认并告警。
// 日志经全局 default_logger() 输出。
SelfTestConfig load_config(const std::string& path);

}  // namespace checker
