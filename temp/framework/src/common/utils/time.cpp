/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "common/config_define.h"
#include "common/logger.h"
#include "common/utils/time.h"

std::string GetNow() {
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(now, TimerFormat.begin());
    return ss.str();
}

std::tm GetLocalTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm nowTm = {};

#if defined(_WIN32)
    localtime_s(&nowTm, &nowTime);
#else
    localtime_r(&nowTime, &nowTm);
#endif

    return nowTm;
}

std::string GetTimeString(const std::tm& tm) {
    std::stringstream ss;
    ss << std::put_time(&tm, TimerFormat.begin());
    return ss.str();
}

std::chrono::system_clock::time_point TMToTimePoint(const std::tm& tmTime) {
    std::tm tmCopy = tmTime;

    // 使用 timegm 而不是 mktime（POSIX 系统）
#if defined(_WIN32)
    // Windows 没有 timegm，使用 _mkgmtime
    std::time_t time = _mkgmtime(&tmCopy);
#else
    // Linux/Unix 使用 timegm
    std::time_t time = timegm(&tmCopy);
#endif

    if (time == -1) {
        SLOG_ERROR << "Failed to convert UTC tm to time_t";
    }

    return std::chrono::system_clock::from_time_t(time);
}