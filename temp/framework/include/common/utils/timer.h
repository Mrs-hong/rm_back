/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_TIMER_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_TIMER_H

#include <cstdint>

/**
 * @brief 等待指定的毫秒数
 * @param milliseconds 要等待的毫秒数
 */
void WaitForMilliseconds(uint32_t milliseconds);

/**
 * @brief 等待指定的秒数
 * @param seconds 要等待的秒数
 */
void WaitForSeconds(uint32_t seconds);

/**
 * @brief 等待指定的分钟数
 * @param minutes 要等待的分钟数
 */
void WaitForMinutes(uint32_t minutes);

/**
 * @brief 等待指定的小时数
 * @param hours 要等待的小时数
 */
void WaitForHours(uint32_t hours);

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_TIMER_H