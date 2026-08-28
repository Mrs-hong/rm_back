/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "hal/button/button_gpio_driver.h"

namespace qifeng {
    GPIOButtonDriver::GPIOButtonDriver(const GPIOButtonDeviceConfig &config) : mConfig(config) {
    }

    bool GPIOButtonDriver::Init() {
        mChip.reset(gpiod_chip_open_by_name(mConfig.chip.c_str()));
        if (!mChip) {
            return false;
        }

        mLine.reset(gpiod_chip_get_line(mChip.get(), mConfig.line));
        if (!mLine) {
            mChip.reset();
            return false;
        }

        if (gpiod_line_request_input(mLine.get(), "qifeng_button") < 0) {
            mLine.reset();
            mChip.reset();
            return false;
        }

        return true;
    }

    bool GPIOButtonDriver::ReadLevel(bool &level) {
        if (!mLine) {
            return false;
        }

        int value = gpiod_line_get_value(mLine.get());
        if (value < 0) {
            return false;
        }

        level = value != 0;
        return true;
    }
}  // namespace qifeng
