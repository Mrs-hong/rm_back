/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_FINGERPRINT_FINGERPRINT_TYPES_H
#define HAL_FINGERPRINT_FINGERPRINT_TYPES_H

#include <cstdint>

namespace qifeng {
    struct RegisterResult {
        uint16_t fingerId = 0;
        uint8_t progress = 0;
    };

    struct SaveResult {
        uint16_t fingerId = 0;
    };

    struct MatchResult {
        uint16_t matchResult = 0;
        uint16_t matchScore = 0;
        uint16_t matchId = 0xFFFF;
    };

    struct DeleteResult {
        uint16_t fingerId = 0;
    };

    /**
     * @brief 指纹模组 LED 灯光状态
     * @details 对外仅暴露四种状态，屏蔽模组 PWM/闪烁等细节：
     *          - Off：  关闭 LED
     *          - Red：  红色常亮
     *          - Green：绿色常亮
     *          - BlueBreath：蓝色呼吸灯（PWM，呼吸周期 2s）
     */
    enum class FingerprintLedState : uint8_t {
        Off = 0,
        Red,
        Green,
        BlueBreath,
    };

}  // namespace qifeng

#endif  // HAL_FINGERPRINT_FINGERPRINT_TYPES_H
