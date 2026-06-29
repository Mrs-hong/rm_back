// =============================================================================
// time_util.h — 计时与时间格式化工具
// =============================================================================
#pragma once

#include <chrono>
#include <string>

namespace checker {

// 简易计时器：构造时记录起点，elapsed_ms() 返回毫秒
class Timer {
 public:
  Timer() : start_(std::chrono::steady_clock::now()) {}
  void reset() { start_ = std::chrono::steady_clock::now(); }
  int elapsed_ms() const {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start_)
                                .count());
  }
 private:
  std::chrono::steady_clock::time_point start_;
};

// 当前 UTC 时间 ISO8601 字符串，如 "2026-06-29T12:34:56Z"
std::string now_iso8601();

}  // namespace checker
