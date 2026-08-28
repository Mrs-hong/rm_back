/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_BUTTON_BUTTON_GPIO_DRIVER_H
#define HAL_BUTTON_BUTTON_GPIO_DRIVER_H

#include <gpiod.h>
#include <memory>

#include "button_driver.h"

namespace qifeng {
    /// @brief gpiod_chip 资源的自定义释放器
    struct GPIODChipDeleter {
        void operator()(struct gpiod_chip* p) const {
            gpiod_chip_close(p);
        }
    };

    /// @brief gpiod_line 资源的自定义释放器
    struct GPIODLineDeleter {
        void operator()(struct gpiod_line* p) const {
            gpiod_line_release(p);
        }
    };

    struct GPIOButtonDeviceConfig {
        std::string chip;
        uint32_t line = 0;
    };

    /**
     * @brief GPIO按键驱动，通过libgpiod读取按键电平
     */
    class GPIOButtonDriver : public ButtonDriver {
    public:
        explicit GPIOButtonDriver(const GPIOButtonDeviceConfig& config);

        ~GPIOButtonDriver() override = default;

        GPIOButtonDriver(const GPIOButtonDriver&) = delete;
        GPIOButtonDriver& operator=(const GPIOButtonDriver&) = delete;
        GPIOButtonDriver(GPIOButtonDriver&&) noexcept = default;
        GPIOButtonDriver& operator=(GPIOButtonDriver&&) noexcept = default;

        bool Init() override;

        bool ReadLevel(bool& level) override;

    private:
        GPIOButtonDeviceConfig mConfig;

        std::unique_ptr<struct gpiod_chip, GPIODChipDeleter> mChip;
        std::unique_ptr<struct gpiod_line, GPIODLineDeleter> mLine;
    };
}  // namespace qifeng
#endif  // HAL_BUTTON_BUTTON_GPIO_DRIVER_H
