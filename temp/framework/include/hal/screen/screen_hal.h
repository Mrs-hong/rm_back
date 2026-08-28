/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_SCREEN_HAL_H
#define HAL_SCREEN_SCREEN_HAL_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "hal/screen/protocol/diwen_protocol.h"
#include "hal/screen/protocol/screen_protocol.h"
#include "hal/screen/screen_types.h"
#include "hal/screen/transport/screen_transport.h"
#include "hal/screen/transport/serial_transport.h"

namespace qifeng {
    /**
     * @brief 屏幕操作结果
     */
    enum class ScreenResult : uint8_t {
        OK = 0,
        Timeout,
        NotInitialized,
        InvalidParameter,
        TransportError,
        UpgradeInProgress,
        UpgradeFailed
    };

    enum class ScreenProtocolType : uint8_t { Diwen = 0 };
    enum class ScreenTransportType : uint8_t { Serial = 0 };

    struct DisplayConfig {
        ScreenProtocolType protocolType = ScreenProtocolType::Diwen;
        ScreenTransportType transportType = ScreenTransportType::Serial;
        DiwenProtocolConfig protocol {};
        SerialTransportConfig transport {};
    };

    /**
     * @brief 会话状态
     */
    enum class SessionState : uint8_t {
        Sending,     // 帧正在发送中
        WaitingAck,  // 帧已发送，等待协议层应答
        Completed    // 所有帧已收到应答
    };

    /**
     * @brief 异步发送会话，跟踪请求的发送与协议应答
     *
     * 写请求：多帧，所有帧收到 ACK 后 promise 返回 OK。
     * 读请求：单帧，应答数据写入 readData，promise 返回 OK + 数据。
     */
    struct Session {
        uint64_t id = 0;
        SessionState state = SessionState::Sending;
        bool isRead = false;
        std::promise<std::pair<ScreenResult, std::vector<uint8_t>>> promise;

        // 写请求：多帧发送计数
        uint32_t totalFrames = 0;
        uint32_t sentFrames = 0;
        uint32_t ackedFrames = 0;

        // 读请求：应答匹配与数据
        uint16_t expectedAddr = 0;
        std::vector<uint8_t> readData;
    };

    /**
     * @brief 屏幕硬件抽象层
     *
     * 组合协议编码与传输层，提供页面切换和控件更新能力。
     * 通过会话机制实现端到端确认：发送帧后等待屏幕协议层ACK应答，
     * 超时未收到应答则返回Timeout，可据此判断显示屏链路是否通畅。
     */
    class ScreenHAL {
    public:
        ScreenHAL() = default;

        ~ScreenHAL();

        ScreenHAL(const ScreenHAL&) = delete;
        ScreenHAL& operator=(const ScreenHAL&) = delete;
        ScreenHAL(ScreenHAL&&) = delete;
        ScreenHAL& operator=(ScreenHAL&&) = delete;

        /**
         * @brief 初始化屏幕HAL，根据配置创建协议和传输层实例
         * @param config 显示屏配置
         * @return 操作结果
         */
        ScreenResult Init(const DisplayConfig& config);

        /**
         * @brief 释放协议和传输层资源
         */
        void Release();

        /**
         * @brief 切换显示页面（差量：页面未变化时跳过发送）
         * @param page 目标页面
         * @param timeoutMs 超时时间(毫秒)
         * @return 操作结果
         */
        ScreenResult SwitchPage(DisplayPage page, uint32_t timeoutMs);

        /**
         * @brief 更新控件数据（差量：仅发送与缓存不同的字段）
         * @param type 控件类型
         * @param data 控件数据，需与 type 匹配
         * @param timeoutMs 超时时间(毫秒)
         * @return 操作结果
         */
        ScreenResult UpdateWidget(WidgetType type, const WidgetData& data, uint32_t timeoutMs);

        /**
         * @brief 清除差量缓存，下次调用将强制全量发送
         */
        void InvalidateCache();

        /**
         * @brief 串口固件升级
         *
         * 依次升级每个文件，全部完成后发一次重启命令使屏幕生效。
         * 升级期间 SwitchPage/UpdateWidget 返回 UpgradeInProgress。
         * @param filePaths 固件文件路径列表（文件名前缀数字为库号）
         * @param timeoutMsPerOp 单次发送应答超时(毫秒)
         * @return 操作结果，失败码区分文件/超时/Flash 写入等
         */
        ScreenResult UpgradeFirmware(const std::vector<std::string>& filePaths, uint32_t timeoutMsPerOp = 3000);

    private:
        // ── 控件请求构建 ──
        bool BuildWidgetRequests(WidgetType type, const WidgetData& data, std::vector<Request>& out);
        void BuildIdleAvailability(const IdleAvailabilityData& data, std::vector<Request>& out);
        void BuildIdleSummary(const IdleSummaryData& data, std::vector<Request>& out);
        void BuildIdleTask(const IdleTaskData& data, std::vector<Request>& out);
        void BuildIdleStatus(const IdleStatusData& data, std::vector<Request>& out);
        void BuildMeetingBasic(const MeetingBasicData& data, std::vector<Request>& out);
        void BuildMeetingInfo(const MeetingInfoData& data, std::vector<Request>& out);
        void BuildMeetingEnergy(const MeetingEnergyData& data, std::vector<Request>& out);
        bool BuildMeetingClock(const MeetingClockData& data, std::vector<Request>& out);
        void BuildDeviceInfo(const DeviceInfoData& data, std::vector<Request>& out);
        void BuildFingerprint(const FingerprintData& data, std::vector<Request>& out);
        void BuildUpgrade(const UpgradeData& data, std::vector<Request>& out);
        static std::string SanitizeDisplayText(const std::string& text, size_t maxGbkBytes = 0);

        // ── 差量缓存 ──
        void FilterUnchangedRequests(std::vector<Request>& requests);
        void UpdateCache(const std::vector<Request>& requests);

        // ── 会话管理与传输 ──
        void OnSendComplete(uint64_t sessionId);
        void OnReceiveData(const uint8_t* data, size_t len);
        void HandleAckFrame();
        void HandleReadAck(const Frame& frame);

        // ── 固件升级 ──
        std::vector<std::string> SortUpgradeFiles(const std::vector<std::string>& filePaths);
        ScreenResult UpgradeSingleFile(const std::string& filePath, uint32_t timeoutMsPerOp);
        bool PollFlashComplete(uint32_t timeoutMs);
        ScreenResult TriggerFlashWrite(uint16_t blockAddr, uint32_t timeoutMsPerOp);
        ScreenResult SendDataChunks(const std::vector<uint8_t>& fileData, size_t offset, uint16_t& addr,
                                    uint32_t timeoutMsPerOp);
        bool WaitForFlashIdle(uint32_t waitMs);
        ScreenResult SendReboot(uint32_t timeoutMs);
        static bool ParseLibId(const std::string& filePath, uint16_t& outId);

        // ── 请求发送 ──
        ScreenResult SendRequests(std::vector<std::pair<uint64_t, std::vector<uint8_t>>>&& frames, uint32_t timeoutMs);
        ScreenResult SendReadRequest(const std::vector<uint8_t>& frame, uint16_t expectedAddr,
                                     std::vector<uint8_t>& outData, uint32_t timeoutMs);

        // ── 协议与传输层 ──
        std::unique_ptr<IScreenProtocol> mProtocol {};
        std::unique_ptr<ScreenTransport> mTransport {};

        // ── 会话状态 ──
        std::mutex mSessionMutex;
        std::unordered_map<uint64_t, std::unique_ptr<Session>> mSessions {};
        std::deque<uint64_t> mPendingAckQueue {};
        std::atomic<uint64_t> mNextSessionId {1};
        std::atomic<bool> mInitialized {false};

        // ── 接收缓冲 ──
        std::mutex mReceiveMutex;
        std::vector<uint8_t> mReceiveBuffer {};

        // ── 字段级差量缓存 ──
        std::mutex mCacheMutex;
        std::unordered_map<uint16_t, Value> mFieldCache {};
        std::optional<DisplayPage> mCurrentPage;

        // ── 固件升级状态 ──
        std::atomic<bool> mUpgradeInProgress {false};
    };

}  // namespace qifeng

#endif  // HAL_SCREEN_SCREEN_HAL_H
