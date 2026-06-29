// =============================================================================
// logger.hpp — 日志器（基于 spdlog 的外观封装）
//
// 保持原有 printf 风格 API（LOG_INFO("fmt %s", arg)），内部委托 spdlog。
// 双 sink 输出：
//   1) 彩色控制台 (stderr)
//   2) 滚动文件 (按大小滚动，线程安全)
//
// 用法：
//   checker::default_logger().init("/var/log/bm1684-selftest", "selftest.log");
//   LOG_INFO("hello %s", "world");
// =============================================================================
#pragma once

#include <cstdarg>
#include <cstdint>
#include <string>

namespace checker {

// 日志级别（与 spdlog level 对应）
enum class LogLevel {
  kTrace = 0,
  kDebug,
  kInfo,
  kWarn,
  kError,
};

class Logger {
 public:
  Logger() = default;
  ~Logger();

  // 初始化：log_dir 为空则仅输出到控制台
  void init(const std::string& log_dir, const std::string& filename = "selftest.log",
            LogLevel level = LogLevel::kInfo, size_t max_file_bytes = 1 << 20,
            size_t max_files = 3);

  void set_level(LogLevel level);

  // 核心写接口（线程安全）。printf 风格格式串，内部格式化后委托 spdlog。
  void log(LogLevel level, const char* fmt, ...) const;

 private:
  // 将 printf 风格格式串转为固定字符串（vsnprintf）
  static std::string format_msg(const char* fmt, va_list ap);

  void* impl_ = nullptr;  // pImpl 指向 spdlog logger，避免头文件暴露 spdlog
  LogLevel level_ = LogLevel::kInfo;
};

// 全局默认日志器（main 中初始化，供宏使用）
Logger& default_logger();

}  // namespace checker

// --- 便捷日志宏 -------------------------------------------------------------
// 使用全局 default_logger()，调用方无需到处传递 logger 引用。
#define LOG_TRACE(fmt, ...) \
  ::checker::default_logger().log(::checker::LogLevel::kTrace, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) \
  ::checker::default_logger().log(::checker::LogLevel::kDebug, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) \
  ::checker::default_logger().log(::checker::LogLevel::kInfo, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) \
  ::checker::default_logger().log(::checker::LogLevel::kWarn, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) \
  ::checker::default_logger().log(::checker::LogLevel::kError, fmt, ##__VA_ARGS__)
