/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <chrono>
#include <thread>

#include "common/utils/timer.h"

void WaitForMilliseconds(uint32_t milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void WaitForSeconds(uint32_t seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

void WaitForMinutes(uint32_t minutes) {
    std::this_thread::sleep_for(std::chrono::minutes(minutes));
}

void WaitForHours(uint32_t hours) {
    std::this_thread::sleep_for(std::chrono::hours(hours));
}