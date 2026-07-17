/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/types.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace qifeng::scm::utils {
    /**
     * @brief 将 systemd 微秒时间戳格式化为 "YYYY-MM-DD HH:MM:SS.mmm"
     * @param usec 自 epoch 起的微秒时间戳
     * @return std::string 格式化后的时间字符串
     */
    inline std::string FormatTimestamp(uint64_t usec) {
        auto timePoint = std::chrono::system_clock::time_point(std::chrono::microseconds(usec));
        auto timeTNow = std::chrono::system_clock::to_time_t(timePoint);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&timeTNow), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    /**
     * @brief 计算从 startUsec 到当前的运行时长，格式化为 "Xd HH:MM:SS"
     * @param startUsec 起始时间（自 epoch 的微秒时间戳）
     * @return std::string 格式化后的运行时长字符串
     */
    inline std::string FormatDuration(uint64_t startUsec) {
        auto now = std::chrono::system_clock::now();
        auto start = std::chrono::system_clock::time_point(std::chrono::microseconds(startUsec));
        auto diff = now - start;

        if (diff.count() <= 0) {
            return "0d 00:00:00";
        }

        auto totalSec = std::chrono::duration_cast<std::chrono::seconds>(diff).count();
        int days = static_cast<int>(totalSec / 86400);
        int hours = static_cast<int>((totalSec % 86400) / 3600);
        int minutes = static_cast<int>((totalSec % 3600) / 60);
        int seconds = static_cast<int>(totalSec % 60);

        std::stringstream ss;
        ss << days << "d ";
        ss << std::setfill('0') << std::setw(2) << hours << ":";
        ss << std::setfill('0') << std::setw(2) << minutes << ":";
        ss << std::setfill('0') << std::setw(2) << seconds;
        return ss.str();
    }

    /**
     * @brief 计算 CPU 占用百分比
     * @details 公式：CPUUsageNSec / 运行秒数 / 1e9 * 100
     * @param cpuUsageNSec 进程累计 CPU 时间（纳秒）
     * @param activeEnterUsec 进程启动时间（自 epoch 的微秒时间戳）
     * @return size_t CPU 占用百分比（0-100）
     */
    inline size_t CalculateCpuUsage(uint64_t cpuUsageNSec, uint64_t activeEnterUsec) {
        auto now = std::chrono::system_clock::now();
        auto start = std::chrono::system_clock::time_point(std::chrono::microseconds(activeEnterUsec));
        auto runSec = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        if (runSec <= 0) {
            return 0;
        }
        double cpuSec = static_cast<double>(cpuUsageNSec) / 1e9;
        double percent = (cpuSec / static_cast<double>(runSec)) * 100.0;
        return static_cast<size_t>(percent);
    }
}  // namespace qifeng::scm::utils
