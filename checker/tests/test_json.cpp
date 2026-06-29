// =============================================================================
// test_json.cpp — JSON 与配置加载自测（基于 nlohmann/json）
// 验证：JSON 解析/取值/序列化往返 + load_config 配置加载正确性。
// =============================================================================
#include <cassert>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <string>

#include "checker/core/context.h"

using namespace checker;
using json = nlohmann::json;

static int failures = 0;

#define CHECK(cond)                                                  \
  do {                                                               \
    if (!(cond)) {                                                   \
      std::fprintf(stderr, "FAIL: %s @ line %d\n", #cond, __LINE__); \
      ++failures;                                                    \
    }                                                                \
  } while (0)

// 测试 1：nlohmann/json 基本解析与取值
void test_json_basic() {
  const char* text = R"({
    "per_item_timeout_sec": 5,
    "parallel": true,
    "disk": { "mounts": ["/", "/data"], "min_free_pct": 5 },
    "gateway": "192.168.1.1"
  })";

  json v = json::parse(text);
  CHECK(v.is_object());
  CHECK(v["per_item_timeout_sec"].get<int>() == 5);
  CHECK(v["parallel"].get<bool>() == true);
  CHECK(v["gateway"].get<std::string>() == "192.168.1.1");
  CHECK(v["disk"]["min_free_pct"].get<int>() == 5);
  CHECK(v["disk"]["mounts"].is_array());
  CHECK(v["disk"]["mounts"].size() == 2);
  CHECK(v["disk"]["mounts"][0].get<std::string>() == "/");

  // 缺失键检测
  CHECK(!v.contains("no_such_key"));

  // 往返序列化
  std::string out = v.dump();
  json v2 = json::parse(out);
  CHECK(v2["per_item_timeout_sec"].get<int>() == 5);

  // 构造并序列化
  json built;
  built["name"] = "disk";
  built["elapsed_ms"] = 12;
  built["ok"] = true;
  std::string s = built.dump(2);
  CHECK(s.find("\"name\": \"disk\"") != std::string::npos);
}

// 测试 2：load_config 加载实际配置文件
void test_config_load() {
  SelfTestConfig cfg = load_config("config/selftest.json");
  CHECK(cfg.per_item_timeout_sec == 5);
  CHECK(cfg.parallel == true);
  CHECK(cfg.disk.mounts.size() == 3);
  CHECK(cfg.disk.mounts[0] == "/");
  CHECK(cfg.disk.min_free_pct == 5);
  CHECK(cfg.memory.min_available_mb == 128);
  CHECK(cfg.fingerprint.device == "/dev/ttyS3");
  CHECK(cfg.fingerprint.baud == 57600);
  CHECK(cfg.network.gateway == "192.168.1.1");
  CHECK(cfg.model.path == "/opt/sophon/selftest/probe.bmodel");
}

int main() {
  test_json_basic();
  test_config_load();

  if (failures == 0) {
    std::printf("json+config tests PASSED\n");
    return 0;
  }
  std::printf("json+config tests FAILED (%d)\n", failures);
  return 1;
}
