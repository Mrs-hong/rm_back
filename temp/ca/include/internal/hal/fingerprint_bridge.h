//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_HAL_FINGERPRINT_BRIDGE_H
#define QIFENG_CA_INCLUDE_INTERNAL_HAL_FINGERPRINT_BRIDGE_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <shared_mutex>

#include "internal/hal/hal_types.h"
#include "qifeng_framework/hal/fingerprint/fingerprint_types.h"

namespace qifeng {
    class FingerprintHAL;
    struct FingerprintEvent;
}  // namespace qifeng

namespace qifeng_ca {

    // 指纹操作结果
    enum class FingerprintResult : uint8_t {
        OK = 0,
        NotInitialized,
        Timeout,
        InvalidParameter,
        DeviceBusy,
        DeviceError,
        DuplicateFinger,   // 指纹重复
        SmallContactArea,  // 手指未完整贴合采集区
        PoorImageQuality,  // 指纹纹路未识别(图像质量差)
        MoveTooMuch,       // 手指移动幅度过大
        MoveTooLittle,     // 手指移动幅度过小
        StorageFull,       // 指纹存储已满
        CaptureFail,       // 采集失败
        EnrollFailed,      // 多次采集后失败
        IdentifyFailed,    // 识别失败
        NotFound           // 指纹未找到
    };

    // 指纹录入进度
    struct EnrollProgress {
        uint8_t mProgress {0};
        bool mCompleted {false};
        uint16_t mFingerId {0};
    };

    // 指纹匹配结果
    struct MatchResult {
        uint16_t mFingerId {0xFFFF};
        uint16_t mScore {0};
        bool mMatched {false};
    };

    // 指纹保存结果
    struct SaveResult {
        uint16_t mFingerId {0};
        bool mSuccess {false};
    };

    // 指纹事件回调类型
    using FingerprintCallback = std::function<void(const FingerprintEventData &)>;

    class FingerprintBridge {
    public:
        static FingerprintBridge &GetInstance();

        // ---- 生命周期 ----
        bool Init();
        void Release();
        bool IsInitialized() const;

        // ---- 回调 ----
        void SetCallback(FingerprintCallback callback);

        // ---- 指纹操作 ----
        // 单次采集(regIdx: 1-6), 返回采集结果与进度
        FingerprintResult EnrollCapture(uint8_t regIdx, EnrollProgress &progress, uint32_t timeoutMs);
        // 保存指纹模板(fingerId为设备端存储ID)
        FingerprintResult SaveTemplate(uint16_t fingerId, SaveResult &result, uint32_t timeoutMs);
        // 指纹识别(1:N匹配)
        FingerprintResult Verify(MatchResult &result, uint32_t timeoutMs);
        // 删除指纹
        FingerprintResult Delete(uint16_t fingerId, uint32_t timeoutMs);
        // 取消当前操作
        FingerprintResult Cancel(uint32_t timeoutMs);
        // 设置指纹模组LED灯光状态
        FingerprintResult SetLed(qifeng::FingerprintLedState state, uint32_t timeoutMs);

    private:
        FingerprintBridge() = default;
        ~FingerprintBridge() = default;
        FingerprintBridge(const FingerprintBridge &) = delete;
        FingerprintBridge(FingerprintBridge &&) = delete;
        FingerprintBridge &operator=(const FingerprintBridge &) = delete;
        FingerprintBridge &operator=(FingerprintBridge &&) = delete;

        // 框架回调入口, 转换事件后分发到用户回调
        void OnFrameworkEvent(const qifeng::FingerprintEvent &event);

        template <typename Fn>
        FingerprintResult RetryOperation(Fn &&op);

        std::unique_ptr<qifeng::FingerprintHAL> mFingerprintHal;
        std::atomic<bool> mInitialized {false};

        mutable std::shared_mutex mCallbackMutex;
        FingerprintCallback mCallback;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_HAL_FINGERPRINT_BRIDGE_H
