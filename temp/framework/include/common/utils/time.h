/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_TIME_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_TIME_H

#include <chrono>
#include <ctime>
#include <string>

/**
 * @brief 获取当前时间的年月日时分秒格式字符串
 * @return 格式为"YYYY-MM-DD HH:MM:SS"的当前时间字符串
 */
std::string GetNow();

std::tm GetLocalTime();

std::string GetTimeString(const std::tm& tm);

std::chrono::system_clock::time_point TMToTimePoint(const std::tm& tmTime);

inline uint64_t GetTimeMs() {
    timespec ts {};
    clock_gettime(CLOCK_REALTIME_COARSE, &ts);

    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

inline uint64_t GetTimeUs() {
    timespec ts {};

    clock_gettime(CLOCK_REALTIME, &ts);

    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
}

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_TIME_H
