/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <filesystem>
#include <iostream>
#include <netdb.h>
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "common/config_define.h"
#include "common/logger.h"

enum class Logger::Level {
    TRACE = spdlog::level::trace,
    DEBUG = spdlog::level::debug,
    INFO = spdlog::level::info,
    WARN = spdlog::level::warn,
    ERR = spdlog::level::err,
    CRITICAL = spdlog::level::critical,
    OFF = spdlog::level::off
};

Logger& Logger::GetInstance() {
    static Logger Instance;
    return Instance;
}

bool Logger::Initialize(const std::string& serviceName) {
    return Initialize(std::string(Log::DefaultLogDir), serviceName + std::string(Log::DefaultLogFileSuffix),
                      Log::DefaultMaxFileSize, Log::DefaultMaxFiles);
}

bool Logger::Initialize(const std::string& logDir, const std::string& baseFileName, size_t maxFileSize,
                        size_t maxFiles) {
    if (mLogger) {
        spdlog::warn("Logger is already initialized.");
        return true;
    }
    try {
        // 创建日志目录(如果不存在)
        if (!std::filesystem::exists(logDir)) {
            if (!std::filesystem::create_directories(logDir)) {
                std::cerr << "Failed to create log directory: " << logDir << std::endl;
                return false;
            }
        }

        // 日志文件路径
        std::filesystem::path logDirPath(logDir);
        std::filesystem::path logFilePath = logDirPath / baseFileName;

        // 创建滚动文件输出器(线程安全版本)
        auto fileSink =
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePath, maxFileSize, maxFiles, true);

        // 组合多个sink
        std::vector<spdlog::sink_ptr> sinks {fileSink};

        // 创建异步logger
        mThread = std::make_shared<spdlog::details::thread_pool>(Log::DefaultLogQueueSize, Log::DefaultLogThreadCnt);
        mLogger = std::make_shared<spdlog::async_logger>("file_sink", sinks.begin(), sinks.end(), mThread);

        // 设置日志格式：时间 | 进程ID | 线程ID | 日志级别 | 消息
        mLogger->set_pattern(std::string(Log::DefaultLogFileFormat));

        // 设置刷新策略
        mLogger->flush_on(spdlog::level::warn);

        // 设置定时刷新(每30秒)
        spdlog::flush_every(std::chrono::seconds(Log::DefaultLogFlushInterval));
        // 注册logger
        spdlog::register_logger(mLogger);
        spdlog::set_default_logger(mLogger);

        SLOG_INFO << "Logger initialized successfully. Log directory: " << logDir;
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << "\n";
        return false;
    } catch (const std::exception& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << "\n";
        return false;
    }
}

Logger::~Logger() {
    // 关闭日志器，确保所有日志都被写入
    Flush();
}

void Logger::Trace(const char* file, int line, const std::string& comment) {
    if (mLogger) {
        mLogger->trace("[{}:{}] {}", GetFileName(file), line, comment);
    }
}

void Logger::Debug(const char* file, int line, const std::string& comment) {
    if (mLogger) {
        mLogger->debug("[{}:{}] {}", GetFileName(file), line, comment);
    }
}

void Logger::Info(const char* file, int line, const std::string& comment) {
    if (mLogger) {
        mLogger->info("[{}:{}] {}", GetFileName(file), line, comment);
    }
}

void Logger::Warn(const char* file, int line, const std::string& comment) {
    if (mLogger) {
        mLogger->warn("[{}:{}] {}", GetFileName(file), line, comment);
    }
}

void Logger::Error(const char* file, int line, const std::string& comment) {
    if (mLogger) {
        mLogger->error("[{}:{}] {}", GetFileName(file), line, comment);
    }
}

void Logger::Critical(const char* file, int line, const std::string& comment) {
    if (mLogger) {
        mLogger->critical("[{}:{}] {}", GetFileName(file), line, comment);
    }
}

void Logger::SetLevel(Level level) {
    if (mLogger) {
        mLogger->set_level(static_cast<spdlog::level::level_enum>(level));
    }
}
