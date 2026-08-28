/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_LED_LED_DRIVER_H
#define HAL_LED_LED_DRIVER_H

#include <cstdint>

namespace qifeng {
    enum class LedColor : uint8_t { Off = 0, Red, Green, Blue };
    enum class MicMuteStatus : uint8_t { Unmute = 0, Mute, Unavailable };

    inline bool LedColorToMuteStatus(LedColor color, MicMuteStatus& status) {
        switch (color) {
            case LedColor::Red:
                status = MicMuteStatus::Mute;
                return true;
            case LedColor::Green:
                status = MicMuteStatus::Unmute;
                return true;
            case LedColor::Blue:
            case LedColor::Off:
            default:
                return false;
        }
    }

    /**
     * @brief LED驱动抽象接口
     */
    class LedDriver {
    public:
        virtual ~LedDriver() = default;

        LedDriver(const LedDriver&) = delete;
        LedDriver& operator=(const LedDriver&) = delete;
        LedDriver(LedDriver&&) = delete;
        LedDriver& operator=(LedDriver&&) = delete;

        /**
         * @brief 初始化LED驱动
         * @return 初始化成功返回true
         */
        virtual bool Init() = 0;

        /**
         * @brief 设置LED颜色
         * @param color 目标颜色
         * @return 设置成功返回true
         */
        virtual bool SetColor(LedColor color) = 0;

        /**
         * @brief 获取当前LED颜色
         * @return 当前颜色
         */
        virtual LedColor GetColor() const = 0;

        /**
         * @brief 释放LED驱动资源
         */
        virtual void Release() = 0;

        /**
         * @brief 查询麦克风真实静音状态
         * @return 静音状态
         */
        virtual MicMuteStatus QueryMuteStatus() {
            return MicMuteStatus::Unavailable;
        }

        /**
         * @brief 获取最近一次外部工具调用的退出码
         * @return 退出码（0=成功，3=HID 通信失败/设备未连接；GPIO 等非 CLI 驱动恒为 0）
         */
        virtual int LastExitCode() const {
            return 0;
        }

    protected:
        LedDriver() = default;
    };

}  // namespace qifeng

#endif  // HAL_LED_LED_DRIVER_H
