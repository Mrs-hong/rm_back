/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_LED_LED_GPIO_DRIVER_H
#define HAL_LED_LED_GPIO_DRIVER_H

#include <cstdint>

#include "led_driver.h"

namespace qifeng {
    struct GPIOLedConfig {
        uint32_t red_gpio = 401;
        uint32_t green_gpio = 402;
    };

    /**
     * @brief GPIO LED驱动，通过sysfs控制双色LED
     */
    class GPIOLedDriver : public LedDriver {
    public:
        explicit GPIOLedDriver(const GPIOLedConfig& config);

        ~GPIOLedDriver() override;

        GPIOLedDriver(const GPIOLedDriver&) = delete;
        GPIOLedDriver& operator=(const GPIOLedDriver&) = delete;
        GPIOLedDriver(GPIOLedDriver&&) = delete;
        GPIOLedDriver& operator=(GPIOLedDriver&&) = delete;

        bool Init() override;
        bool SetColor(LedColor color) override;
        LedColor GetColor() const override;
        void Release() override;

    private:
        GPIOLedConfig mConfig;
        LedColor mCurrentColor {LedColor::Off};
        bool mInitialized {false};
    };

}  // namespace qifeng

#endif  // HAL_LED_LED_GPIO_DRIVER_H
