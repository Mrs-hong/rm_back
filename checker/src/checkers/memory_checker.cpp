// =============================================================================
// memory_checker.cpp — 内存自检
// =============================================================================
#include "checker/checkers/memory_checker.h"

#include <sys/sysinfo.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "checker/log/logger.hpp"
#include "checker/util/time_util.h"

namespace checker {

namespace {

// 从 /proc/meminfo 读取指定字段(KB)
long long read_meminfo_kb(const std::string& key) {
  std::ifstream f("/proc/meminfo");
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind(key, 0) == 0) {
      long long v = 0;
      if (sscanf(line.c_str() + key.size(), ": %lld kB", &v) == 1) return v;
    }
  }
  return -1;
}

}  // namespace

CheckResult MemoryChecker::run(const Context& ctx) {
  CheckResult r(name());
  Timer timer;

  struct sysinfo si;
  bool sys_ok = sysinfo(&si) == 0;

  long long total_kb = read_meminfo_kb("MemTotal");
  long long avail_kb = read_meminfo_kb("MemAvailable");
  if (total_kb < 0) total_kb = sys_ok ? (long long)(si.totalram >> 10) : 0;
  if (avail_kb < 0) avail_kb = sys_ok ? (long long)(si.freeram >> 10) : 0;

  char buf[128];
  snprintf(buf, sizeof(buf), "total=%lldMB avail=%lldMB", total_kb >> 10, avail_kb >> 10);
  r.details.emplace_back("meminfo", buf);

  // 检测 ECC 信息（若 /sys/devices/system/edac/mc 存在）
  std::ifstream edac("/sys/devices/system/edac/mc/mc0/ce_count");
  if (edac) {
    int ce = 0;
    edac >> ce;
    r.details.emplace_back("ecc_correctable", std::to_string(ce));
  }

  long long need_kb = (long long)ctx.config.memory.min_available_mb << 10;
  bool ok = (avail_kb >= need_kb) && (total_kb > 0);
  r.status = ok ? Status::kPass : Status::kFail;
  r.message = ok ? "memory ok" : "memory insufficient";
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[memory] %s (%dms)", r.message.c_str(), r.elapsed_ms);
  return r;
}

}  // namespace checker
