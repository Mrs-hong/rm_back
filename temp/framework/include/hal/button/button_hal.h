/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_BUTTON_BUTTON_HAL_H
#define HAL_BUTTON_BUTTON_HAL_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

#include "button_driver.h"
#include "button_gpio_driver.h"
#include "debouncer.h"
#include "hal/bounded_queue.h"

namespace qifeng {
    enum class ButtonEventType : uint8_t { ShortPress = 0 };

    enum class ButtonDeviceType : uint8_t { GPIO = 0 };

    struct ButtonItemConfig {
        uint32_t id = 0;
        ButtonDeviceType type = ButtonDeviceType::GPIO;
        GPIOButtonDeviceConfig gpio {};
    };

    struct ButtonConfig {
        std::vector<ButtonItemConfig> buttons {};
    };

    struct ButtonEvent {
        uint32_t buttonId = 0;
        ButtonEventType type = ButtonEventType::ShortPress;
    };

    struct ButtonContext {
        uint32_t id = 0;
        ButtonItemConfig config;
        std::unique_ptr<ButtonDriver> driver;
        std::unique_ptr<Debouncer> debouncer;
        bool prevPressed = false;
    };

    using ButtonCallback = std::function<void(uint32_t buttonId, ButtonEventType event)>;

    /**
     * @brief 按键硬件抽象层
     *
     * 管理多个按键设备的轮询、消抖和事件分发
     */
    class ButtonHAL {
    public:
        ButtonHAL() = default;

        ~ButtonHAL();

        ButtonHAL(const ButtonHAL&) = delete;
        ButtonHAL& operator=(const ButtonHAL&) = delete;
        ButtonHAL(ButtonHAL&&) = delete;
        ButtonHAL& operator=(ButtonHAL&&) = delete;

        /**
         * @brief 初始化按键HAL
         * @param config 按键配置，包含所有按键的设备信息
         * @return 初始化成功返回true
         */
        bool Init(const ButtonConfig& config);

        /**
         * @brief 设置按键事件回调
         * @param callback 按键事件触发时调用的回调函数
         * @return 设置成功返回true
         */
        bool SetButtonCallback(ButtonCallback callback);

        /**
         * @brief 释放资源，停止轮询线程
         */
        void Release();

    private:
        void EventCollectionLoop();

        void EventCallbackLoop();

    private:
        static constexpr uint32_t DebounceMs = 50;
        static constexpr uint32_t PollIntervalMs = 10;
        static constexpr size_t DefaultCapacity = 64;

        std::atomic<bool> mRunning {false};
        std::atomic<bool> mInitialized {false};
        std::vector<ButtonContext> mButtonContexts {};
        BoundedQueue<ButtonEvent> mEventQueue {DefaultCapacity};

        ButtonCallback mCallback {};
        std::thread mEventThread;
        std::thread mCallbackThread;
    };

}  // namespace qifeng

#endif  // HAL_BUTTON_BUTTON_HAL_H
