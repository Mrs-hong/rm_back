//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_HAL_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_HAL_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class HalConfig {
    public:
        static HalConfig &GetInstance() {
            static HalConfig Instance;
            return Instance;
        }

        // ---- Button配置 ----
        std::string GetButtonGpioChip() const {
            return CONFIG_MANAGER.GetString("hal_button", "gpio_chip", "gpiochip3");
        }

        uint32_t GetButtonGpioLine() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_button", "gpio_line", 0));
        }

        uint32_t GetButtonId() const { return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_button", "id", 0)); }

        uint32_t GetButtonDebounceMs() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_button", "debounce_ms", 50));
        }

        uint32_t GetButtonLongPressMs() const {
            int limitMax = CONFIG_MANAGER.GetInt("hal_button", "long_press_ms", 2000);
            return (limitMax < 0 || limitMax > 60000) ? 2000 : static_cast<uint32_t>(limitMax);
        }

        // ---- LED配置 ----
        // TODO(yf): 暂留，后续由HAL提供是否带外连麦克风能力
        uint32_t GetLedId() const { return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_led", "id", 0)); }

        uint32_t GetLedRedGpio() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_led", "red_gpio", 401));
        }

        uint32_t GetLedGreenGpio() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_led", "green_gpio", 402));
        }

        uint32_t GetLedBlueGpio() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_led", "blue_gpio", 403));
        }
        std::string GetCliPath() const { return CONFIG_MANAGER.GetString("hal_led", "cli_path", "bin/mic-led-ctl"); }

        uint32_t GetCliTimeoutMs() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_led", "cli_timeout_ms", 500));
        }

        // ---- Record配置 ----
        // 麦克风设备清单(分号分隔,按顺序优先尝试)
        // std::vector<std::string> GetRecordPcmNames() const {
        //     const std::string raw =
        //         CONFIG_MANAGER.GetString("hal_record", "pcm_names", "hw:CARD=MKMC500,DEV=0");
        //     std::vector<std::string> names;
        //     size_t start = 0;
        //     while (start <= raw.size()) {
        //         size_t pos = raw.find(';', start);
        //         if (pos == std::string::npos) {
        //             pos = raw.size();
        //         }
        //         if (pos > start) {
        //             names.push_back(raw.substr(start, pos - start));
        //         }
        //         if (pos == raw.size()) {
        //             break;
        //         }
        //         start = pos + 1;
        //     }
        //     return names;
        // }

        std::string GetRecordPcmNames() const {
            return CONFIG_MANAGER.GetString("hal_record", "pcm_name", "hw:CARD=M702,DEV=0");
        }

        // 麦克风设备黑名单(分号分隔), 自动探测时跳过这些设备
        std::vector<std::string> GetRecordBlacklistPcmNames() const {
            const std::string raw = CONFIG_MANAGER.GetString(
                "hal_record", "blacklist_pcm_names", "hw:CARD=rockchiphdmiin,DEV=0;hw:CARD=rockchipes8388,DEV=0");
            std::vector<std::string> names;
            size_t start = 0;
            while (start <= raw.size()) {
                size_t pos = raw.find(';', start);
                if (pos == std::string::npos) {
                    pos = raw.size();
                }
                if (pos > start) {
                    names.push_back(raw.substr(start, pos - start));
                }
                if (pos == raw.size()) {
                    break;
                }
                start = pos + 1;
            }
            return names;
        }

        // ---- Record配置 - HAL的采样率等 ----
        uint32_t GetRecordSampleRate() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_record", "sample_rate", 48000));
        }

        uint16_t GetRecordChannels() const {
            return static_cast<uint16_t>(CONFIG_MANAGER.GetInt("hal_record", "channels", 2));
        }

        uint16_t GetRecordBitDepth() const {
            return static_cast<uint16_t>(CONFIG_MANAGER.GetInt("hal_record", "bit_depth", 16));
        }

        uint32_t GetRecordLatencyMs() const {
            int limitMax = CONFIG_MANAGER.GetInt("hal_record", "latency_ms", 100);
            return limitMax < 0 ? 100 : static_cast<uint32_t>(limitMax);
        }

        uint32_t GetRecordRingBufferSize() const {
            static constexpr int DefaultValue = 12 * 1024 * 1024;
            int limitMax = CONFIG_MANAGER.GetInt("hal_record", "ring_buffer_size", DefaultValue);
            return limitMax < 1024 ? static_cast<uint32_t>(DefaultValue) : static_cast<uint32_t>(limitMax);
        }

        // ---- Record配置 - BMS落盘的采样率等 ----
        uint32_t GetFileRecordSampleRate() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_record", "src_sample_rate", 48000));
        }

        uint16_t GetFileRecordChannels() const {
            return static_cast<uint16_t>(CONFIG_MANAGER.GetInt("hal_record", "src_channels", 2));
        }

        uint16_t GetFileRecordBitDepth() const {
            return static_cast<uint16_t>(CONFIG_MANAGER.GetInt("hal_record", "src_bit_depth", 16));
        }

        // ---- Display配置 ----
        std::string GetDisplaySerialPort() const {
            return CONFIG_MANAGER.GetString("hal_display", "serial_port", "/dev/ttyS2");
        }

        uint32_t GetDisplayBaudRate() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_display", "baud_rate", 115200));
        }

        uint32_t GetDisplayTimeoutMs() const {
            int limitMax = CONFIG_MANAGER.GetInt("hal_display", "timeout_ms", 500);
            return (limitMax < 0 || limitMax > 60000) ? 500 : limitMax;
        }

        // ---- Fingerprint配置 ----
        std::string GetFingerprintDevice() const {
            return CONFIG_MANAGER.GetString("hal_fingerprint", "device", "/dev/ttyACM1");
        }

        uint32_t GetFingerprintBaudRate() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_fingerprint", "baud_rate", 57600));
        }

        // TODO(yf): 指纹待定
        uint32_t GetFingerprintTimeoutMs() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_fingerprint", "timeout_ms", 3000));
        }

        bool IsFingerprintBridge() const {
            return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_fingerprint", "fingerprint", 0)) > 0;
        }

        // 跳过按键(指纹)初始化
        bool IsSkipButton() const { return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_button", "skip", 0)) > 0; }

        bool IsScreen() const { return static_cast<uint32_t>(CONFIG_MANAGER.GetInt("hal_screen", "screen", 1)) > 0; }

    private:
        HalConfig() = default;
        ~HalConfig() = default;
        HalConfig(const HalConfig &) = delete;
        HalConfig &operator=(const HalConfig &) = delete;
        HalConfig(HalConfig &&) = delete;
        HalConfig &operator=(HalConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_HAL_CONFIG_H
