/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "hal/led/led_gpio_driver.h"

#include "common/logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>

namespace qifeng {
    namespace {
        constexpr std::string_view ExportPath = "/sys/class/gpio/export";
        constexpr std::string_view UnexportPath = "/sys/class/gpio/unexport";

        std::string GPIOBasePath(uint32_t gpio) {
            return "/sys/class/gpio/gpio" + std::to_string(gpio);
        }

        bool GPIOIsExported(uint32_t gpio) {
            return std::filesystem::exists(GPIOBasePath(gpio));
        }

        bool WriteToFile(const std::string& path, const std::string& content, uint32_t gpio) {
            std::ofstream ofs(path);
            if (!ofs) {
                SLOG_ERROR << "Failed to open " << path << " for GPIO " << gpio;
                return false;
            }
            ofs << content;
            ofs.close();
            const bool success = !ofs.fail();
            if (!success) {
                SLOG_ERROR << "Failed to write " << path << " for GPIO " << gpio;
            }
            return success;
        }

        bool ExportGPIO(uint32_t gpio) {
            if (GPIOIsExported(gpio)) {
                return true;
            }

            if (!WriteToFile(std::string(ExportPath), std::to_string(gpio), gpio)) {
                return false;
            }

            for (int i = 0; i < 10; ++i) {
                if (GPIOIsExported(gpio)) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            SLOG_ERROR << "GPIO " << gpio << " directory not found after export";
            return false;
        }

        bool UnexportGPIO(uint32_t gpio) {
            if (!GPIOIsExported(gpio)) {
                return true;
            }
            return WriteToFile(std::string(UnexportPath), std::to_string(gpio), gpio);
        }

        bool SetGPIODirection(uint32_t gpio, const std::string& direction) {
            std::string path = GPIOBasePath(gpio) + "/direction";
            return WriteToFile(path, direction, gpio);
        }

        bool WriteGPIOValue(uint32_t gpio, const std::string& value) {
            std::string path = GPIOBasePath(gpio) + "/value";
            return WriteToFile(path, value, gpio);
        }
    }  // namespace

    GPIOLedDriver::GPIOLedDriver(const GPIOLedConfig& config) : mConfig(config) {
    }

    GPIOLedDriver::~GPIOLedDriver() {
        Release();
    }

    bool GPIOLedDriver::Init() {
        if (mInitialized) {
            SLOG_WARN << "GPIOLedDriver already initialized";
            return true;
        }

        if (!ExportGPIO(mConfig.red_gpio)) {
            SLOG_ERROR << "Failed to export red GPIO " << mConfig.red_gpio;
            return false;
        }

        if (!ExportGPIO(mConfig.green_gpio)) {
            SLOG_ERROR << "Failed to export green GPIO " << mConfig.green_gpio;
            return false;
        }

        if (!SetGPIODirection(mConfig.red_gpio, "out")) {
            SLOG_ERROR << "Failed to set direction for red GPIO " << mConfig.red_gpio;
            return false;
        }

        if (!SetGPIODirection(mConfig.green_gpio, "out")) {
            SLOG_ERROR << "Failed to set direction for green GPIO " << mConfig.green_gpio;
            return false;
        }

        if (!WriteGPIOValue(mConfig.red_gpio, "0") || !WriteGPIOValue(mConfig.green_gpio, "0")) {
            SLOG_ERROR << "Failed to initialize Led GPIO values";
            return false;
        }

        mInitialized = true;
        mCurrentColor = LedColor::Off;

        SLOG_INFO << "GPIOLedDriver initialized: red=" << mConfig.red_gpio << " green=" << mConfig.green_gpio;
        return true;
    }

    bool GPIOLedDriver::SetColor(LedColor color) {
        if (!mInitialized) {
            SLOG_ERROR << "GPIOLedDriver not initialized";
            return false;
        }

        switch (color) {
            case LedColor::Off:
                if (!WriteGPIOValue(mConfig.red_gpio, "0") || !WriteGPIOValue(mConfig.green_gpio, "0")) {
                    SLOG_ERROR << "Failed to turn off Led";
                    return false;
                }
                break;
            case LedColor::Red:
                if (!WriteGPIOValue(mConfig.red_gpio, "1") || !WriteGPIOValue(mConfig.green_gpio, "0")) {
                    SLOG_ERROR << "Failed to set Led to red";
                    return false;
                }
                break;
            case LedColor::Green:
                if (!WriteGPIOValue(mConfig.red_gpio, "0") || !WriteGPIOValue(mConfig.green_gpio, "1")) {
                    SLOG_ERROR << "Failed to set Led to green";
                    return false;
                }
                break;
            case LedColor::Blue:
            default:
                SLOG_ERROR << "Unsupported Led color: " << static_cast<int>(color);
                return false;
        }

        mCurrentColor = color;
        return true;
    }

    LedColor GPIOLedDriver::GetColor() const {
        return mCurrentColor;
    }

    void GPIOLedDriver::Release() {
        if (!mInitialized) {
            return;
        }

        WriteGPIOValue(mConfig.red_gpio, "0");
        WriteGPIOValue(mConfig.green_gpio, "0");
        UnexportGPIO(mConfig.red_gpio);
        UnexportGPIO(mConfig.green_gpio);

        mInitialized = false;
        mCurrentColor = LedColor::Off;
    }

}  // namespace qifeng
