/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_FINGERPRINT_FINGERPRINT_HAL_H
#define HAL_FINGERPRINT_FINGERPRINT_HAL_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hal/bounded_queue.h"
#include "hal/fingerprint/fingerprint_types.h"
#include "hal/fingerprint/protocol/fingerprint_protocol.h"
#include "hal/fingerprint/transport/fingerprint_transport.h"

namespace qifeng {
    enum class FingerprintResult : uint8_t {
        OK = 0,
        Timeout,           // 操作超时
        NotInitialized,    // 未初始化
        InvalidParameter,  // 无效参数
        DuplicateFinger,   // 指纹重复
        SmallContactArea,  // 小接触面积
        DeviceBusy,        // 设备忙
        DeviceError,       // 设备错误
        QueueFull,         // 队列已满
        Canceled           // 操作被取消
    };
    enum class FingerprintProtocolType : uint8_t { Hlk = 0 };
    enum class FingerprintTransportType : uint8_t { Serial = 0 };

    struct FingerprintSerialConfig {
        std::string port = "/dev/ttyUSB0";
        uint32_t baudRate = 57600;
    };

    struct FingerprintConfig {
        FingerprintProtocolType protocolType = FingerprintProtocolType::Hlk;
        FingerprintTransportType transportType = FingerprintTransportType::Serial;
        FingerprintSerialConfig transport {};
    };

    enum class FingerprintEventType : uint8_t {
        FingerDown = 0,  // 手指按下
        FingerUp,        // 手指抬起
        Record,          // 录音事件
        Recognizing,     // 识别中
        RecognizingEnd,  // 识别结束
    };

    struct FingerprintEvent {
        FingerprintEventType type = FingerprintEventType::FingerDown;
        bool matched = false;
        uint16_t fingerId = 0xFFFF;
        uint16_t score = 0;
    };

    using FingerprintCallback = std::function<void(const FingerprintEvent& event)>;

    enum class FpHalState : uint8_t {
        Idle = 0,
        Enrolling,
        Verifying,
    };

    // 模组为单命令请求-响应协议，同一时刻仅允许一个命令占用通道
    struct CmdChannel {
        enum class Phase : uint8_t {
            Idle,       // 空闲，可承载新命令
            Waiting,    // 命令已发送，等待响应
            Completed,  // 已收到响应
            Canceled,   // 被 Cancel 打断
        };

        bool IsIdle() const { return phase == Phase::Idle; }
        bool IsDone() const { return phase == Phase::Completed || phase == Phase::Canceled; }

        Phase phase = Phase::Idle;
        uint16_t cmd = 0;   // 在途命令字
        Frame response {};  // 在途命令的响应帧
    };

    /**
     * @brief 指纹硬件抽象层
     *
     * 组合协议编码与传输层，提供指纹注册、保存、匹配、删除能力。
     * 通过会话机制实现端到端确认：发送请求后等待模组响应帧，
     * 超时未收到响应则返回Timeout。
     */
    class FingerprintHAL {
    public:
        FingerprintHAL() = default;
        ~FingerprintHAL();

        FingerprintHAL(const FingerprintHAL&) = delete;
        FingerprintHAL& operator=(const FingerprintHAL&) = delete;
        FingerprintHAL(FingerprintHAL&&) = delete;
        FingerprintHAL& operator=(FingerprintHAL&&) = delete;

        /**
         * @brief 初始化指纹设备
         * @param config 指纹配置参数
         * @return FingerprintResult
         */
        FingerprintResult Init(const FingerprintConfig& config);

        /**
         * @brief 释放资源
         */
        void Release();

        /**
         * @brief 设置指纹事件回调
         * @param callback 事件触发时调用的回调函数
         * @return 设置成功返回true
         */
        bool SetCallback(FingerprintCallback callback);

        /**
         * @brief 指纹注册（单次采集）
         *
         * 流程：发送手指按下事件 → 轮询手指在位 → 录入 → 等500ms → 查询结果 → 发送手指松开事件 → 轮询手指离开 → 返回
         * @param regIdx 注册次数索引，范围1-6
         * @param[out] result 注册结果
         * @param timeoutMs 等待响应超时时间（ms）
         * @return FingerprintResult
         */
        FingerprintResult Enroll(uint8_t regIdx, RegisterResult& result, uint32_t timeoutMs);

        /**
         * @brief 保存指纹模板
         *
         * 流程：保存 → 等500ms → 查询结果 → 返回
         * @param fingerId 指纹编号
         * @param[out] result 保存结果
         * @param timeoutMs 等待响应超时时间（ms）
         * @return FingerprintResult
         */
        FingerprintResult Save(uint16_t fingerId, SaveResult& result, uint32_t timeoutMs);

        /**
         * @brief 删除指纹
         *
         * 流程：删除 → 等500ms → 查询结果 → 返回
         * @param fingerId 指纹编号
         * @param[out] result 删除结果
         * @param timeoutMs 等待响应超时时间（ms）
         * @return FingerprintResult
         */
        FingerprintResult DeleteFinger(uint32_t fingerId, DeleteResult& result, uint32_t timeoutMs);

        /**
         * @brief 指纹识别（1:N匹配）
         *
         * 流程：状态变更为Verifying → 发起指纹识别 → 等500ms → 查询识别结果 → 状态恢复Idle → 返回
         * @param[out] result 匹配结果
         * @param timeoutMs 等待响应超时时间（ms）
         * @return FingerprintResult
         */
        FingerprintResult Verify(MatchResult& result, uint32_t timeoutMs);

        /**
         * @brief 取消当前操作
         * @param timeoutMs 等待响应超时时间（ms）
         * @return FingerprintResult
         */
        FingerprintResult Cancel(uint32_t timeoutMs);

        /**
         * @brief 设置指纹模组 LED 灯光状态
         * @param state 目标灯光状态
         * @param timeoutMs 等待响应超时时间（ms）
         * @return FingerprintResult
         */
        FingerprintResult SetLed(FingerprintLedState state, uint32_t timeoutMs);

    private:
        // --- 协议命令封装 ---
        FingerprintResult DetectFinger(bool& detected, uint32_t timeoutMs);
        FingerprintResult WaitFingerPresent(uint32_t timeoutMs);
        FingerprintResult WaitFingerRelease(uint32_t timeoutMs);
        FingerprintResult RegisterFingerprint(uint8_t regIdx, uint32_t timeoutMs);
        FingerprintResult QueryRegisterResult(RegisterResult& result, uint32_t timeoutMs);
        FingerprintResult SaveFingerprintTemplate(uint16_t fingerId, uint32_t timeoutMs);
        FingerprintResult QuerySaveResult(SaveResult& result, uint32_t timeoutMs);
        FingerprintResult MatchFingerprint(uint32_t timeoutMs);
        FingerprintResult QueryMatchResult(MatchResult& result, uint32_t timeoutMs);
        FingerprintResult Delete(uint32_t fingerId, uint32_t timeoutMs);
        FingerprintResult QueryDeleteResult(DeleteResult& result, uint32_t timeoutMs);
        FingerprintResult CancelOperation(uint32_t timeoutMs);
        FingerprintResult SetLedControl(FingerprintLedState state, uint32_t timeoutMs);
        FingerprintResult SelfLearn(uint16_t fingerId, uint32_t timeoutMs);

        // --- 通信基础设施 ---
        FingerprintResult SendAndWait(const Request& req, Frame& response, uint32_t timeoutMs);
        FingerprintResult GuardCmd(uint16_t cmd);
        void InterruptActiveCmd();
        void OnSendComplete(uint64_t cmd);
        void OnReceiveData(const uint8_t* data, size_t len);

        // --- 线程与事件 ---
        void EventCallbackLoop();
        void AutoDetectLoop();
        void TriggerRecord();
        bool WaitForIdle();
        void PostEvent(FingerprintEventType type);

        // --- 工具 ---
        bool SetState(FpHalState state);
        FpHalState GetState() const;
        static FingerprintResult MapDeviceError(uint32_t errorCode);
        static uint16_t ParseBE16(const uint8_t* data);

        // --- 常量 ---
        static constexpr size_t KEventQueueCapacity = 32;
        static constexpr uint32_t KQueryDelayMs = 500;
        static constexpr uint32_t KQueryRetryIntervalMs = 200;
        static constexpr uint32_t KFingerPollIntervalMs = 200;
        static constexpr uint32_t KAutoDetectIntervalMs = 150;
        static constexpr uint32_t KFingerHoldThresholdMs = 1500;
        static constexpr uint32_t KAutoDetectTimeoutMs = 200;

        // --- 成员变量 ---
        std::unique_ptr<IFingerprintProtocol> mProtocol;
        std::unique_ptr<FingerprintTransport> mTransport;

        std::mutex mChannelMutex;
        std::condition_variable mChannelCV;
        CmdChannel mChannel {};  // 由 mChannelMutex 保护

        std::atomic<bool> mInitialized {false};

        mutable std::mutex mStateMutex;
        std::condition_variable mStateCV;
        FpHalState mState {FpHalState::Idle};

        std::mutex mReceiveMutex;
        std::vector<uint8_t> mReceiveBuffer;

        BoundedQueue<FingerprintEvent> mEventQueue {KEventQueueCapacity};
        mutable std::mutex mCallbackMutex;
        FingerprintCallback mCallback {};
        std::thread mCallbackThread;
        std::thread mAutoDetectThread;
    };

}  // namespace qifeng

#endif  // HAL_FINGERPRINT_FINGERPRINT_HAL_H
