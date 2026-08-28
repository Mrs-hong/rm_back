/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_BUTTON_DEBOUNCER_H
#define HAL_BUTTON_DEBOUNCER_H

#include <chrono>
#include <cstdint>

namespace qifeng {
    /**
     * @brief 按键消抖器
     *
     * 基于时间窗口的消抖策略，输入信号必须保持稳定超过
     * debounceMs 才会更新输出值
     */
    struct Debouncer {
        uint32_t debounceMs;
        bool stableValue = false;
        std::chrono::steady_clock::time_point lastChangeTime = std::chrono::steady_clock::now();

        explicit Debouncer(uint32_t ms = 50) : debounceMs(ms) {
        }

        bool Update(bool input) {
            auto now = std::chrono::steady_clock::now();
            if (input != stableValue) {
                if (now - lastChangeTime >= std::chrono::milliseconds(debounceMs)) {
                    stableValue = input;
                }
            } else {
                lastChangeTime = now;
            }
            return stableValue;
        }

        void Reset(bool value) {
            stableValue = value;
            lastChangeTime = std::chrono::steady_clock::now();
        }
    };
}  // namespace qifeng

#endif  // HAL_BUTTON_DEBOUNCER_H
