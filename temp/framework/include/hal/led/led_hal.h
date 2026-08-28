/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_LED_LED_HAL_H
#define HAL_LED_LED_HAL_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "hal/bounded_queue.h"
#include "led_cli_driver.h"
#include "led_driver.h"
#include "led_gpio_driver.h"

namespace qifeng {
    enum class LedDeviceType : uint8_t { GPIO = 0, CLI = 1 };
    enum class LedMode : uint8_t {
        Off = 0,
        On,
    };

    struct LedItemConfig {
        uint32_t id = 0;
        LedDeviceType type = LedDeviceType::GPIO;
        GPIOLedConfig gpio {};
        CliLedConfig cli {};
    };

    struct LedConfig {
        std::vector<LedItemConfig> leds {};
    };

    struct LedContext {
        uint32_t id = 0;
        LedItemConfig config;
        std::unique_ptr<LedDriver> driver;
    };

    /// @brief LED 麦克风事件类型
    enum class LedEventType : uint8_t {
        Mute = 0,  // 麦克风被物理静音
        Unmute,    // 麦克风解除静音
    };

    struct LedEvent {
        uint32_t ledId = 0;
        LedEventType type = LedEventType::Mute;
    };

    using LedCallback = std::function<void(uint32_t ledId, LedEventType event)>;

    /**
     * @brief LED硬件抽象层
     *
     * 管理多个LED设备的颜色和开关状态
     */
    class LedHAL {
    public:
        LedHAL() = default;

        ~LedHAL();

        LedHAL(const LedHAL&) = delete;
        LedHAL& operator=(const LedHAL&) = delete;
        LedHAL(LedHAL&&) = delete;
        LedHAL& operator=(LedHAL&&) = delete;

        /**
         * @brief 初始化LED HAL
         * @param config LED配置，包含所有LED的设备信息
         * @return 初始化成功返回true
         */
        bool Init(const LedConfig& config);

        /**
         * @brief 设置LED颜色和模式
         * @param mode LED模式（开/关）
         * @param color LED颜色
         * @return 设置成功返回true
         */
        bool SetColor(LedMode mode, LedColor color);

        /**
         * @brief 释放资源，关闭所有LED
         */
        void Release();

        /// @brief 设置麦克风状态事件回调（仅 CLI 模式生效）
        bool SetLedCallback(LedCallback callback);

    private:
        void MonitorLoop();
        void EventCallbackLoop();

        LedMode mMode = LedMode::Off;
        LedColor mColor = LedColor::Red;

        std::vector<LedContext> mLedContexts {};
        std::atomic<bool> mInitialized {false};
        mutable std::mutex mMutex;

        // 麦克风状态监控（仅 CLI 模式）
        std::atomic<bool> mRunning {false};
        LedDriver* mCliDriver = nullptr;
        uint32_t mCliLedId = 0;
        MicMuteStatus mDesiredStatus {MicMuteStatus::Mute};
        MicMuteStatus mObservedStatus {MicMuteStatus::Mute};
        MicMuteStatus mLastReportedStatus {MicMuteStatus::Mute};
        bool mInRecovery = false;
        LedCallback mCallback {};
        BoundedQueue<LedEvent> mEventQueue {8};
        std::thread mMonitorThread;
        std::thread mCallbackThread;
        std::condition_variable mMonitorCv;
    };

}  // namespace qifeng

#endif  // HAL_LED_LED_HAL_H
