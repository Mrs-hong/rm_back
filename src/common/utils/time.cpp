/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/time.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace qifeng::scm::utils {
    /**
     * @brief 获取当前本地时间的格式化字符串
     * @return std::string 格式 "YYYY-MM-DD HH:MM:SS"
     */
    std::string GetCurrentTimeString() {
        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        struct tm tmStruct {};
        localtime_r(&nowTime, &tmStruct);
        std::ostringstream oss;
        oss << std::put_time(&tmStruct, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
}  // namespace qifeng::scm::utils
