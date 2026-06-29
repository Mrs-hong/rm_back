// =============================================================================
// logger.cpp — 日志器实现（基于 spdlog）
//
// 内部用 spdlog 创建双 sink（彩色控制台 + 滚动文件）日志器。
// 对外保持 printf 风格 API：log() 先用 vsnprintf 格式化，再委托 spdlog 输出。
// 这样所有调用方（LOG_INFO("%s", x) 等）无需改动。
// =============================================================================
#include "checker/log/logger.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace checker {

namespace {
// 全局默认日志器实例
Logger g_default;
}  // namespace

Logger& default_logger() { return g_default; }

// spdlog 级别映射
static spdlog::level::level_enum to_spdlog_level(LogLevel l) {
  switch (l) {
    case LogLevel::kTrace:
      return spdlog::level::trace;
    case LogLevel::kDebug:
      return spdlog::level::debug;
    case LogLevel::kInfo:
      return spdlog::level::info;
    case LogLevel::kWarn:
      return spdlog::level::warn;
    case LogLevel::kError:
      return spdlog::level::err;
  }
  return spdlog::level::info;
}

Logger::~Logger() {
  // 释放 spdlog logger（impl_ 存储 shared_ptr<spdlog::logger>*）
  if (impl_) {
    auto* p = static_cast<std::shared_ptr<spdlog::logger>*>(impl_);
    delete p;
    impl_ = nullptr;
  }
}

std::string Logger::format_msg(const char* fmt, va_list ap) {
  // 先尝试栈缓冲，不够则动态分配
  char stackbuf[1024];
  va_list ap2;
  va_copy(ap2, ap);
  int len = std::vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap2);
  va_end(ap2);
  if (len < 0) return "(log format error)";
  if (static_cast<size_t>(len) < sizeof(stackbuf)) {
    return std::string(stackbuf, static_cast<size_t>(len));
  }
  // 动态分配
  std::vector<char> buf(static_cast<size_t>(len) + 1);
  std::vsnprintf(buf.data(), buf.size(), fmt, ap);
  return std::string(buf.data(), static_cast<size_t>(len));
}

void Logger::init(const std::string& log_dir, const std::string& filename, LogLevel level,
                  size_t max_file_bytes, size_t max_files) {
  level_ = level;

  // 释放旧 logger
  if (impl_) {
    auto* p = static_cast<std::shared_ptr<spdlog::logger>*>(impl_);
    delete p;
    impl_ = nullptr;
  }

  // 构建 sink 列表
  std::vector<spdlog::sink_ptr> sinks;
  // 彩色控制台 (stderr)
  sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
  // 滚动文件（log_dir 为空则跳过）
  if (!log_dir.empty()) {
    ::mkdir(log_dir.c_str(), 0755);
    std::string path = log_dir + "/" + filename;
    try {
      sinks.push_back(
          std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, max_file_bytes, max_files));
    } catch (const std::exception& e) {
      // 文件 sink 创建失败（目录不可写等）：仅控制台输出，不中断
      std::fprintf(stderr, "[logger] file sink init failed: %s\n", e.what());
    }
  }

  auto logger = std::make_shared<spdlog::logger>("selftest", sinks.begin(), sinks.end());
  logger->set_level(to_spdlog_level(level));
  // [时间] [级别] 消息
  logger->set_pattern("%Y-%m-%d %H:%M:%S [%^%l%$] %v");
  logger->flush_on(spdlog::level::warn);  // warn 及以上立即刷新

  impl_ = new std::shared_ptr<spdlog::logger>(std::move(logger));
}

void Logger::set_level(LogLevel level) {
  level_ = level;
  if (impl_) {
    auto* p = static_cast<std::shared_ptr<spdlog::logger>*>(impl_);
    if (*p) (*p)->set_level(to_spdlog_level(level));
  }
}

void Logger::log(LogLevel level, const char* fmt, ...) const {
  if (static_cast<int>(level) < static_cast<int>(level_)) return;
  if (!impl_) return;

  va_list ap;
  va_start(ap, fmt);
  std::string msg = format_msg(fmt, ap);
  va_end(ap);

  auto* p = static_cast<std::shared_ptr<spdlog::logger>*>(impl_);
  if (*p) {
    switch (level) {
      case LogLevel::kTrace:
        (*p)->trace(msg);
        break;
      case LogLevel::kDebug:
        (*p)->debug(msg);
        break;
      case LogLevel::kInfo:
        (*p)->info(msg);
        break;
      case LogLevel::kWarn:
        (*p)->warn(msg);
        break;
      case LogLevel::kError:
        (*p)->error(msg);
        break;
    }
  }
}

}  // namespace checker
