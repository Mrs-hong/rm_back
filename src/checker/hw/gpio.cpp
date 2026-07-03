/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/config.h"  // IWYU pragma: keep — provides CHECKER_HAS_GPIOD
#include "checker/hw/gpio.h"

#if CHECKER_HAS_GPIOD

    #include <gpiod.h>

    #include <cerrno>
    #include <cstring>

    #include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    // NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
    Gpio::Gpio(const std::string &number) {
        // Parse pin identifier: "global_number" or "chip_index:offset"
        auto colonPos = number.find(':');

        if (colonPos != std::string::npos) {
            // "chip_index:offset" format, e.g. "0:16"
            int chipIdx = std::stoi(number.substr(0, colonPos));
            auto offset = static_cast<unsigned int>(std::stoul(number.substr(colonPos + 1)));

            std::string path = "/dev/gpiochip" + std::to_string(chipIdx);
            mChip = gpiod_chip_open(path.c_str());
            if (!mChip) {
                SLOG_DEBUG << "gpiod: cannot open " << path << ": " << strerror(errno);
                mOk = false;
                return;
            }
            mOwnsChip = true;  // opened by us, must close on destruction

            mLine = gpiod_chip_get_line(mChip, offset);
            if (!mLine) {
                SLOG_DEBUG << "gpiod: cannot get line " << offset << " on " << path << ": " << strerror(errno);
                gpiod_chip_close(mChip);
                mChip = nullptr;
                mOwnsChip = false;
                mOk = false;
                return;
            }
        } else {
            // Global number format, e.g. "488"
            // Iterate gpiochip devices, use gpiod_chip_find_line to locate
            int gpioNum = std::stoi(number);
            bool found = false;

            for (int i = 0; i < 16; ++i) {
                std::string path = "/dev/gpiochip" + std::to_string(i);
                struct gpiod_chip* chip = gpiod_chip_open(path.c_str());
                if (!chip) {
                    continue;
                }

                // gpiod_chip_find_line: find line by name within this chip
                // Line name format is "gpioN" (e.g. "gpio488")
                std::string lineName = "gpio" + number;
                struct gpiod_line* line = gpiod_chip_find_line(chip, lineName.c_str());
                if (line) {
                    mChip = chip;
                    mLine = line;
                    mOwnsChip = true;  // we opened the chip, must close on destruction
                    found = true;
                    break;
                }
                gpiod_chip_close(chip);
            }

            if (!found) {
                SLOG_DEBUG << "gpiod: gpio " << gpioNum << " not found on any chip";
                mOk = false;
                return;
            }
        }

        // Request output mode (initial low)
        struct gpiod_line_request_config config {};
        std::memset(&config, 0, sizeof(config));
        config.consumer = "qifeng-checker";
        config.request_type = GPIOD_LINE_REQUEST_DIRECTION_OUTPUT;
        config.flags = 0;

        int defaultVal = 0;
        int ret = gpiod_line_request(mLine, &config, defaultVal);
        if (ret < 0) {
            SLOG_DEBUG << "gpiod: request output failed: " << strerror(errno);
            gpiod_line_release(mLine);
            mLine = nullptr;
            if (mOwnsChip && mChip) {
                gpiod_chip_close(mChip);
                mChip = nullptr;
                mOwnsChip = false;
            }
            mOk = false;
            return;
        }

        mOk = true;
    }

    Gpio::~Gpio() {
        if (mLine) {
            gpiod_line_release(mLine);
        }
        // Only close chip if we opened it via gpiod_chip_open
        if (mOwnsChip && mChip) {
            gpiod_chip_close(mChip);
        }
    }

    bool Gpio::Set(bool high) {
        if (!mOk) {
            return false;
        }
        return gpiod_line_set_value(mLine, high ? 1 : 0) == 0;
    }

    bool Gpio::Get(bool &high) {
        if (!mOk) {
            return false;
        }
        int val = gpiod_line_get_value(mLine);
        if (val < 0) {
            return false;
        }
        high = (val == 1);
        return true;
    }

}  // namespace qifeng::scm

#else  // !CHECKER_HAS_GPIOD

// Stub implementation when libgpiod is not available
// All operations return false, LightChecker will report Skipped
namespace qifeng::scm {

    Gpio::Gpio(const std::string & /*number*/) : mOk(false) {
    }

    Gpio::~Gpio() = default;

    bool Gpio::Set(bool /*high*/) {
        return false;
    }

    bool Gpio::Get(bool & /*high*/) {
        return false;
    }

}  // namespace qifeng::scm

#endif  // CHECKER_HAS_GPIOD