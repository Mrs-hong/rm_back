/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_LED_LED_CLI_DRIVER_H
#define HAL_LED_LED_CLI_DRIVER_H

#include <cstdint>
#include <string>

#include "led_driver.h"

namespace qifeng {
    struct CliLedConfig {
        std::string tool_path {"/usr/local/bin/mic-led-ctl"};
        uint32_t timeout_ms {2000};
    };

    /**
     * @brief CLI 指示灯驱动，通过调用外部命令行工具控制指示灯
     *
     * 颜色映射（mic-led-ctl）：
     *   Red -> mute（静音红灯）
     *   Green / Off -> unmute（正常绿灯/灭灯）
     *   Blue -> 不支持
     */
    class CliLedDriver : public LedDriver {
    public:
        explicit CliLedDriver(const CliLedConfig& config);

        ~CliLedDriver() override;

        CliLedDriver(const CliLedDriver&) = delete;
        CliLedDriver& operator=(const CliLedDriver&) = delete;
        CliLedDriver(CliLedDriver&&) = delete;
        CliLedDriver& operator=(CliLedDriver&&) = delete;

        bool Init() override;
        bool SetColor(LedColor color) override;
        LedColor GetColor() const override;
        void Release() override;
        MicMuteStatus QueryMuteStatus() override;
        int LastExitCode() const override;

    private:
        bool SpawnChild(const std::string& action, int& read_fd, pid_t& pid, std::string& err);
        bool ReadWithTimeout(int read_fd, std::string& out, bool& timed_out);
        bool ReapChild(pid_t pid, bool force_kill, std::string& err, int& exit_code);
        bool CallTool(const std::string& action, std::string& out);

        CliLedConfig mConfig;
        LedColor mCurrentColor {LedColor::Off};
        bool mInitialized {false};
        int mLastExitCode {0};  // mic-led-ctl 最近一次退出码：0=成功, 3=未连接, 2=超时
    };

}  // namespace qifeng

#endif  // HAL_LED_LED_CLI_DRIVER_H
