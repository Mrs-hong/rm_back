/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMWORK_COMMON_LOGGER_H
#define QIFENG_FRAMWORK_COMMON_LOGGER_H

#include <memory>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>

// 从完整路径中提取文件名
inline std::string GetFileName(const char* filePath) {
    std::string path(filePath);
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

class Logger {
public:
    // 禁止拷贝
    Logger(const Logger&) = delete;
    Logger(Logger&&) noexcept = default;
    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&) noexcept = default;

    // 日志级别
    enum class Level;

    // 获取单例实例
    static Logger& GetInstance();

    bool Initialize(const std::string& serviceName);

    // 初始化Logger
    bool Initialize(const std::string& logDir, const std::string& baseFileName, size_t maxFileSize, size_t maxFiles);

    // 动态修改日志级别
    void SetLevel(Level level);

    // 函数式调用 - 带文件名和行号
    void Trace(const char* file, int line, const std::string& comment);

    void Debug(const char* file, int line, const std::string& comment);

    void Info(const char* file, int line, const std::string& comment);

    void Warn(const char* file, int line, const std::string& comment);

    void Error(const char* file, int line, const std::string& comment);

    void Critical(const char* file, int line, const std::string& comment);

    // 流式调用 - 不带文件名和行号
    class LogStream {
    public:
        LogStream(std::shared_ptr<spdlog::logger> logger, spdlog::level::level_enum level)
            : mLogger(logger), mLevel(level) {
        }

        LogStream(const LogStream&) = delete;
        LogStream(LogStream&&) noexcept = default;
        LogStream& operator=(const LogStream&) = delete;
        LogStream& operator=(LogStream&&) noexcept = default;

        ~LogStream() {
            if (mLogger) {
                mLogger->log(mLevel, "{}", mStream.str());
            }
        }

        template <typename T>
        LogStream& operator<<(const T& value) {
            mStream << value;
            return *this;
        }

        // 特别处理std::endl等操纵符
        LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
            manip(mStream);
            return *this;
        }

    private:
        std::shared_ptr<spdlog::logger> mLogger;
        spdlog::level::level_enum mLevel;
        std::ostringstream mStream;
    };

    // 流式调用接口
    LogStream Trace() {
        return LogStream(mLogger, spdlog::level::trace);
    }
    LogStream Debug() {
        return LogStream(mLogger, spdlog::level::debug);
    }
    LogStream Info() {
        return LogStream(mLogger, spdlog::level::info);
    }
    LogStream Warn() {
        return LogStream(mLogger, spdlog::level::warn);
    }
    LogStream Error() {
        return LogStream(mLogger, spdlog::level::err);
    }
    LogStream Critical() {
        return LogStream(mLogger, spdlog::level::critical);
    }

    // 刷新日志缓冲区
    void Flush() {
        if (mLogger) {
            mLogger->flush();
        }
    }

    // 检查是否初始化
    bool IsInitialized() const {
        return mLogger != nullptr;
    }

private:
    // 私有构造函数和析构函数
    Logger() = default;
    ~Logger();

    std::shared_ptr<spdlog::logger> mLogger;
    std::shared_ptr<spdlog::details::thread_pool> mThread;
};

// 函数式调用的宏定义
#define FLOG_TRACE(comment) Logger::GetInstance().Trace(__FILE__, __LINE__, comment)
#define FLOG_DEBUG(comment) Logger::GetInstance().Debug(__FILE__, __LINE__, comment)
#define FLOG_INFO(comment) Logger::GetInstance().Info(__FILE__, __LINE__, comment)
#define FLOG_WARN(comment) Logger::GetInstance().Warn(__FILE__, __LINE__, comment)
#define FLOG_ERROR(comment) Logger::GetInstance().Error(__FILE__, __LINE__, comment)
#define FLOG_CRITICAL(comment) Logger::GetInstance().Critical(__FILE__, __LINE__, comment)

#define SLOG_TRACE Logger::GetInstance().Trace() << GetFileName(__FILE__) << ":" << __LINE__ << " "
#define SLOG_DEBUG Logger::GetInstance().Debug() << GetFileName(__FILE__) << ":" << __LINE__ << " "
#define SLOG_INFO Logger::GetInstance().Info() << GetFileName(__FILE__) << ":" << __LINE__ << " "
#define SLOG_WARN Logger::GetInstance().Warn() << GetFileName(__FILE__) << ":" << __LINE__ << " "
#define SLOG_ERROR Logger::GetInstance().Error() << GetFileName(__FILE__) << ":" << __LINE__ << " "
#define SLOG_CRITICAL Logger::GetInstance().Critical() << GetFileName(__FILE__) << ":" << __LINE__ << " "

#endif  // QIFENG_FRAMWORK_COMMON_LOGGER_H
