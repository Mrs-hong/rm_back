/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "hal/button/button_hal.h"

#include "common/logger.h"

namespace qifeng {
    ButtonHAL::~ButtonHAL() {
        Release();
    }

    bool ButtonHAL::Init(const ButtonConfig& config) {
        if (mInitialized.load(std::memory_order_acquire)) {
            SLOG_WARN << "ButtonHAL already initialized";
            return true;
        }

        if (config.buttons.empty()) {
            SLOG_ERROR << "No buttons configured";
            return false;
        }

        for (const auto& btnCfg : config.buttons) {
            ButtonContext ctx;
            ctx.id = btnCfg.id;
            ctx.config = btnCfg;
            ctx.prevPressed = false;

            switch (btnCfg.type) {
                case ButtonDeviceType::GPIO: {
                    auto driver = std::make_unique<GPIOButtonDriver>(btnCfg.gpio);
                    if (!driver->Init()) {
                        SLOG_ERROR << "Failed to init GPIO button driver for button " << btnCfg.id;
                        return false;
                    }
                    ctx.driver = std::move(driver);
                    break;
                }
                default:
                    SLOG_ERROR << "Unsupported button device type for button " << btnCfg.id;
                    return false;
            }

            ctx.debouncer = std::make_unique<Debouncer>(DebounceMs);
            mButtonContexts.push_back(std::move(ctx));
        }

        mRunning.store(true, std::memory_order_release);

        mEventThread = std::thread(&ButtonHAL::EventCollectionLoop, this);
        mCallbackThread = std::thread(&ButtonHAL::EventCallbackLoop, this);

        mInitialized.store(true, std::memory_order_release);

        SLOG_INFO << "ButtonHAL initialized with " << mButtonContexts.size() << " button(s)";
        return true;
    }

    bool ButtonHAL::SetButtonCallback(ButtonCallback callback) {
        if (!callback) {
            SLOG_ERROR << "Button callback is null";
            return false;
        }

        mCallback = std::move(callback);
        return true;
    }

    void ButtonHAL::EventCollectionLoop() {
        const auto pollInterval = std::chrono::milliseconds(PollIntervalMs);

        while (mRunning.load(std::memory_order_acquire)) {
            for (auto& ctx : mButtonContexts) {
                bool level = false;
                bool readOk = ctx.driver->ReadLevel(level);
                if (!readOk) {
                    continue;
                }

                bool rawPressed = !level;
                bool debouncedPressed = ctx.debouncer->Update(rawPressed);

                if (ctx.prevPressed && !debouncedPressed) {
                    ButtonEvent event;
                    event.buttonId = ctx.id;
                    event.type = ButtonEventType::ShortPress;
                    mEventQueue.Push(event);
                    SLOG_INFO << "Button " << ctx.id << " produced ShortPress event";
                }

                ctx.prevPressed = debouncedPressed;
            }

            std::this_thread::sleep_for(pollInterval);
        }
    }

    void ButtonHAL::EventCallbackLoop() {
        while (mRunning.load(std::memory_order_acquire)) {
            ButtonEvent event {};
            if (!mEventQueue.WaitPop(event)) {
                break;
            }

            if (mCallback) {
                mCallback(event.buttonId, event.type);
            }
        }
    }

    void ButtonHAL::Release() {
        if (!mRunning.load(std::memory_order_acquire)) {
            return;
        }

        mRunning.store(false, std::memory_order_release);
        mEventQueue.Stop();

        if (mEventThread.joinable()) {
            mEventThread.join();
        }

        if (mCallbackThread.joinable()) {
            mCallbackThread.join();
        }

        mButtonContexts.clear();
        mInitialized.store(false, std::memory_order_release);

        SLOG_INFO << "ButtonHAL stopped";
    }
}  // namespace qifeng
