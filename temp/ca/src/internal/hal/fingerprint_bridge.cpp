//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <chrono>
#include <thread>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/hal/fingerprint/fingerprint_hal.h"

#include "common/config/hal_config.h"
#include "internal/hal/fingerprint_bridge.h"

namespace qifeng_ca {

    namespace {
        constexpr uint8_t MaxRetries = 3;
        constexpr uint32_t RetryIntervalMs = 200;

        // 仅瞬态错误重试, 永久错误/业务语义错误不重试
        bool IsRetryable(FingerprintResult ret) {
            return ret == FingerprintResult::Timeout || ret == FingerprintResult::DeviceBusy;
        }
    }  // namespace

    // 框架结果转桥接层结果
    static FingerprintResult ConvertResult(qifeng::FingerprintResult raw) {
        switch (raw) {
            case qifeng::FingerprintResult::OK:
                return FingerprintResult::OK;
            case qifeng::FingerprintResult::Timeout:
                return FingerprintResult::Timeout;
            case qifeng::FingerprintResult::NotInitialized:
                return FingerprintResult::NotInitialized;
            case qifeng::FingerprintResult::InvalidParameter:
                return FingerprintResult::InvalidParameter;
            case qifeng::FingerprintResult::DuplicateFinger:
                return FingerprintResult::DuplicateFinger;
            case qifeng::FingerprintResult::SmallContactArea:
                return FingerprintResult::SmallContactArea;
            case qifeng::FingerprintResult::DeviceBusy:
                return FingerprintResult::DeviceBusy;
            default:
                return FingerprintResult::DeviceError;
        }
    }

    FingerprintBridge &FingerprintBridge::GetInstance() {
        static FingerprintBridge Instance;
        return Instance;
    }

    bool FingerprintBridge::Init() {
        if (mInitialized.load(std::memory_order_acquire)) {
            return true;
        }

        auto &cfg = HalConfig::GetInstance();
        qifeng::FingerprintConfig config;
        config.transport.port = cfg.GetFingerprintDevice();
        config.transport.baudRate = cfg.GetFingerprintBaudRate();

        mFingerprintHal = std::make_unique<qifeng::FingerprintHAL>();
        auto result = mFingerprintHal->Init(config);
        if (result != qifeng::FingerprintResult::OK) {
            SLOG_ERROR << "FingerprintBridge: FingerprintHAL init failed";
            mFingerprintHal.reset();
            return false;
        }

        // 注册框架回调, 转发到用户回调
        mFingerprintHal->SetCallback([this](const qifeng::FingerprintEvent &event) { this->OnFrameworkEvent(event); });

        mInitialized.store(true, std::memory_order_release);

        SLOG_INFO << "FingerprintBridge: fingerprint initialized";
        return true;
    }

    void FingerprintBridge::Release() {
        if (!mInitialized.load(std::memory_order_acquire)) {
            return;
        }
        if (mFingerprintHal) {
            mFingerprintHal->Release();
            mFingerprintHal.reset();
        }
        mInitialized.store(false, std::memory_order_release);
        SLOG_INFO << "FingerprintBridge: fingerprint released";
    }

    bool FingerprintBridge::IsInitialized() const {
        return mInitialized.load(std::memory_order_acquire);
    }

    void FingerprintBridge::SetCallback(FingerprintCallback callback) {
        std::unique_lock lock(mCallbackMutex);
        SLOG_INFO << "FingerprintBridge: set callback";
        mCallback = std::move(callback);
    }

    // 框架事件类型转换
    static FingerprintEventType ConvertEventType(qifeng::FingerprintEventType raw) {
        switch (raw) {
            case qifeng::FingerprintEventType::FingerDown:
                return FingerprintEventType::FingerDown;
            case qifeng::FingerprintEventType::FingerUp:
                return FingerprintEventType::FingerUp;
            case qifeng::FingerprintEventType::Record:
                return FingerprintEventType::Record;
            case qifeng::FingerprintEventType::Recognizing:
                return FingerprintEventType::Recognizing;
            case qifeng::FingerprintEventType::RecognizingEnd:
                return FingerprintEventType::RecognizingEnd;
            default:
                return FingerprintEventType::FingerDown;
        }
    }

    void FingerprintBridge::OnFrameworkEvent(const qifeng::FingerprintEvent &event) {
        FingerprintCallback cb;
        {
            std::shared_lock lock(mCallbackMutex);
            cb = mCallback;
        }
        if (!cb) {
            return;
        }

        FingerprintEventData data;
        data.mType = ConvertEventType(event.type);
        data.mMatched = event.matched;
        data.mFingerId = event.fingerId;
        data.mScore = event.score;
        cb(data);
    }

    FingerprintResult FingerprintBridge::EnrollCapture(uint8_t regIdx, EnrollProgress &progress, uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire) || !mFingerprintHal) {
            return FingerprintResult::NotInitialized;
        }
        if (regIdx < 1 || regIdx > 6) {
            return FingerprintResult::InvalidParameter;
        }

        qifeng::RegisterResult rawResult {};
        auto raw = mFingerprintHal->Enroll(regIdx, rawResult, timeoutMs);
        auto ret = ConvertResult(raw);
        if (ret == FingerprintResult::OK) {
            progress.mProgress = rawResult.progress;
            progress.mCompleted = (rawResult.progress >= 100);
            progress.mFingerId = rawResult.fingerId;
        }
        return ret;
    }

    FingerprintResult FingerprintBridge::SaveTemplate(uint16_t fingerId, SaveResult &result, uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire) || !mFingerprintHal) {
            return FingerprintResult::NotInitialized;
        }

        qifeng::SaveResult rawResult {};
        auto raw = mFingerprintHal->Save(fingerId, rawResult, timeoutMs);
        auto ret = ConvertResult(raw);
        if (ret == FingerprintResult::OK) {
            result.mFingerId = rawResult.fingerId;
            result.mSuccess = true;
        }
        return ret;
    }

    FingerprintResult FingerprintBridge::Verify(MatchResult &result, uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire) || !mFingerprintHal) {
            return FingerprintResult::NotInitialized;
        }

        qifeng::MatchResult rawResult {};
        auto raw = mFingerprintHal->Verify(rawResult, timeoutMs);
        auto ret = ConvertResult(raw);
        if (ret == FingerprintResult::OK) {
            result.mMatched = (rawResult.matchResult != 0);
            result.mFingerId = rawResult.matchId;
            result.mScore = rawResult.matchScore;
            if (!result.mMatched) {
                return FingerprintResult::NotFound;
            }
        }
        return ret;
    }

    template <typename Fn>
    FingerprintResult FingerprintBridge::RetryOperation(Fn &&op) {
        FingerprintResult ret {};
        for (uint8_t attempt = 1; attempt <= MaxRetries; ++attempt) {
            ret = op();
            if (ret == FingerprintResult::OK) {
                return ret;
            }
            SLOG_WARN << "FingerprintBridge: op failed, attempt=" << attempt << ", ret=" << static_cast<int>(ret);
            if (!IsRetryable(ret) || attempt == MaxRetries) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(RetryIntervalMs));
        }
        return ret;
    }

    FingerprintResult FingerprintBridge::Delete(uint16_t fingerId, uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire) || !mFingerprintHal) {
            return FingerprintResult::NotInitialized;
        }
        return RetryOperation([&] {
            qifeng::DeleteResult rawResult {};
            auto raw = mFingerprintHal->DeleteFinger(fingerId, rawResult, timeoutMs);
            auto ret = ConvertResult(raw);
            return (ret == FingerprintResult::NotFound) ? FingerprintResult::OK : ret;
        });
    }

    FingerprintResult FingerprintBridge::Cancel(uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire) || !mFingerprintHal) {
            return FingerprintResult::NotInitialized;
        }
        return RetryOperation([&] { return ConvertResult(mFingerprintHal->Cancel(timeoutMs)); });
    }

    FingerprintResult FingerprintBridge::SetLed(qifeng::FingerprintLedState state, uint32_t timeoutMs) {
        if (!mInitialized.load(std::memory_order_acquire) || !mFingerprintHal) {
            return FingerprintResult::NotInitialized;
        }
        return RetryOperation([&] { return ConvertResult(mFingerprintHal->SetLed(state, timeoutMs)); });
    }

}  // namespace qifeng_ca
