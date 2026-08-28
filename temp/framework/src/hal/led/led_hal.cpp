/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "hal/led/led_hal.h"

#include "common/logger.h"

namespace qifeng {
    LedHAL::~LedHAL() {
        Release();
    }

    bool LedHAL::Init(const LedConfig& config) {
        if (mInitialized.load(std::memory_order_acquire)) {
            SLOG_WARN << "LedHAL already initialized";
            return true;
        }

        if (config.leds.empty()) {
            SLOG_ERROR << "No Leds configured";
            return false;
        }

        mMode = LedMode::On;
        mColor = LedColor::Red;
        if (LedColorToMuteStatus(mColor, mDesiredStatus)) {
            mObservedStatus = mDesiredStatus;
            mLastReportedStatus = mDesiredStatus;
        }
        mInRecovery = false;

        for (const auto& ledCfg : config.leds) {
            LedContext ctx;
            ctx.id = ledCfg.id;
            ctx.config = ledCfg;

            switch (ledCfg.type) {
                case LedDeviceType::GPIO: {
                    auto driver = std::make_unique<GPIOLedDriver>(ledCfg.gpio);
                    if (!driver->Init()) {
                        SLOG_ERROR << "Failed to init GPIO Led driver for led " << ledCfg.id;
                        return false;
                    }
                    driver->SetColor(mColor);
                    ctx.driver = std::move(driver);
                    break;
                }
                case LedDeviceType::CLI: {
                    auto driver = std::make_unique<CliLedDriver>(ledCfg.cli);
                    if (!driver->Init()) {
                        SLOG_ERROR << "Failed to init Cli Led driver for led " << ledCfg.id;
                        return false;
                    }
                    driver->SetColor(mColor);
                    mCliDriver = driver.get();
                    mCliLedId = ledCfg.id;
                    ctx.driver = std::move(driver);
                    break;
                }
                default:
                    SLOG_ERROR << "Unsupported Led device type for led " << ledCfg.id;
                    return false;
            }

            mLedContexts.push_back(std::move(ctx));
        }

        mInitialized.store(true, std::memory_order_release);

        SLOG_INFO << "LedHAL initialized with " << mLedContexts.size() << " Led(s)";

        if (mCliDriver != nullptr) {
            mRunning.store(true, std::memory_order_release);
            mMonitorThread = std::thread(&LedHAL::MonitorLoop, this);
            mCallbackThread = std::thread(&LedHAL::EventCallbackLoop, this);
            SLOG_INFO << "LedHAL mic monitor started for cli led " << mCliLedId;
        }

        return true;
    }

    bool LedHAL::SetColor(LedMode mode, LedColor color) {
        if (!mInitialized.load(std::memory_order_acquire)) {
            SLOG_ERROR << "LedHAL not initialized";
            return false;
        }

        std::lock_guard<std::mutex> lock(mMutex);
        bool ok = true;
        for (auto& ctx : mLedContexts) {
            if (ctx.driver && !ctx.driver->SetColor(color)) {
                ok = false;
            }
        }

        if (!ok) {
            if (mCliDriver != nullptr && mCliDriver->LastExitCode() == 3) {
                mObservedStatus = MicMuteStatus::Unavailable;
                mInRecovery = true;
                SLOG_WARN << "LedHAL: mic disconnected (exit code 3), color set failed";
            }
            return false;
        }

        mMode = mode;
        mColor = color;

        MicMuteStatus mapped;
        if (LedColorToMuteStatus(color, mapped)) {
            mDesiredStatus = mapped;
        }

        return true;
    }

    void LedHAL::Release() {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return;
        }

        mRunning.store(false, std::memory_order_release);
        mMonitorCv.notify_all();
        mEventQueue.Stop();
        if (mMonitorThread.joinable())
            mMonitorThread.join();
        if (mCallbackThread.joinable())
            mCallbackThread.join();
        mCliDriver = nullptr;

        std::lock_guard<std::mutex> lock(mMutex);
        for (auto& ctx : mLedContexts) {
            if (ctx.driver) {
                ctx.driver->Release();
            }
        }
        mLedContexts.clear();

        mInitialized.store(false, std::memory_order_release);
        SLOG_INFO << "LedHAL released";
    }

    bool LedHAL::SetLedCallback(LedCallback callback) {
        if (!callback) {
            SLOG_ERROR << "Led callback is null";
            return false;
        }
        mCallback = std::move(callback);
        return true;
    }

    void LedHAL::MonitorLoop() {
        constexpr int pollIntervalMs = 2000;

        while (mRunning.load(std::memory_order_acquire)) {
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mMonitorCv.wait_for(lock, std::chrono::milliseconds(pollIntervalMs),
                                    [this] { return !mRunning.load(std::memory_order_acquire); });
            }
            if (!mRunning.load(std::memory_order_acquire)) {
                break;
            }

            std::lock_guard<std::mutex> lock(mMutex);
            MicMuteStatus status = mCliDriver->QueryMuteStatus();
            mObservedStatus = status;

            if (status == MicMuteStatus::Unavailable) {
                if (!mInRecovery) {
                    SLOG_INFO << "LedHAL: mic disconnected, suspend event polling";
                }
                mInRecovery = true;
                continue;
            }

            if (mInRecovery) {
                MicMuteStatus expected;
                if (LedColorToMuteStatus(mColor, expected)) {
                    mDesiredStatus = expected;
                    if (!mCliDriver->SetColor(mColor)) {
                        SLOG_WARN << "LedHAL: reapply failed on reconnect, keep recovery state";
                        if (mCliDriver->LastExitCode() == 3) {
                            mObservedStatus = MicMuteStatus::Unavailable;
                        }
                        continue;
                    }
                    status = mCliDriver->QueryMuteStatus();
                    mObservedStatus = status;
                    if (status == MicMuteStatus::Unavailable) {
                        SLOG_WARN << "LedHAL: mic disconnected during verify, keep recovery state";
                        continue;
                    }
                }

                // 设备恢复后可能先回到默认 unmute，这一步只同步观测基线，避免把恢复态误报成用户事件。
                mLastReportedStatus = status;
                mInRecovery = false;
                SLOG_INFO << "LedHAL: mic reconnected, suppress recovered status=" << static_cast<int>(status);
                continue;
            }

            if (status == mLastReportedStatus) {
                continue;
            }

            mLastReportedStatus = status;
            if (status == MicMuteStatus::Mute) {
                SLOG_INFO << "LedHAL: mic muted, post Mute event led=" << mCliLedId;
                mEventQueue.Push(LedEvent {mCliLedId, LedEventType::Mute});
            } else {
                SLOG_INFO << "LedHAL: mic unmuted, post Unmute event led=" << mCliLedId;
                mEventQueue.Push(LedEvent {mCliLedId, LedEventType::Unmute});
            }
        }
    }

    void LedHAL::EventCallbackLoop() {
        while (mRunning.load(std::memory_order_acquire)) {
            LedEvent event {};
            if (!mEventQueue.WaitPop(event)) {
                break;
            }
            if (mCallback) {
                mCallback(event.ledId, event.type);
            }
        }
    }

}  // namespace qifeng
