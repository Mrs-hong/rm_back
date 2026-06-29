// =============================================================================
// time_util.cpp — 计时与时间格式化
// =============================================================================
#include "checker/util/time_util.h"

#include <cstdio>
#include <ctime>

namespace checker {

std::string now_iso8601() {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&now, &tm);
  char buf[24];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

}  // namespace checker
