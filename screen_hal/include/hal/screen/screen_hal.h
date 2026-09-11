/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_SCREEN_HAL_H
#define HAL_SCREEN_SCREEN_HAL_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "hal/screen/protocol/screen_protocol.h"
#include "hal/screen/screen_types.h"
#include "hal/screen/transport/screen_transport.h"

namespace qifeng {

    enum class ScreenProtocolType : uint8_t {
        Diwen = 0,  // 迪文 T5L DGUSII 串口屏协议（帧头 5A A5，0x82 写 / 0x83 读）
    };

    enum class ScreenTransportType : uint8_t {
        Serial = 0,  // 串口传输（UART，收发各一线程，默认 /dev/ttyS2 @115200 8N1）
    };

    struct DisplayConfig {
        ScreenProtocolType protocolType = ScreenProtocolType::Diwen;
        ScreenTransportType transportType = ScreenTransportType::Serial;
        DiwenProtocolConfig protocol {};
        SerialTransportConfig transport {};
    };

    /**
     * @brief 屏幕引擎：会话确认与收发原语层
     *
     * 组合协议编码与传输层，提供"请求发送 + 端到端 ACK 确认"与"寄存器读取"
     * 两类原语，以及触控总开关与固件升级能力。
     * 不含任何页面/控件语义——页面归属、差量过滤、弹窗超时等由 ScreenPageManager 负责。
     *
     * 通过会话机制实现端到端确认：发送帧后等待屏幕协议层 ACK 应答，
     * 超时未收到应答则返回 Timeout，可据此判断显示屏链路是否通畅。
     *
     * 触控处理：屏主动上传的 0x83 帧优先匹配挂起的读会话，
     * 未命中再通过 SetTouchCallback 注册的回调上抛给管理层。
     */
    class ScreenEngine {
    public:
        using TouchCallback = std::function<void(const TouchEvent&)>;

        ScreenEngine() = default;
        ~ScreenEngine();

        ScreenEngine(const ScreenEngine&) = delete;
        ScreenEngine& operator=(const ScreenEngine&) = delete;
        ScreenEngine(ScreenEngine&&) = delete;
        ScreenEngine& operator=(ScreenEngine&&) = delete;

        /**
         * @brief 初始化引擎，根据配置创建协议和传输层实例
         */
        ScreenResult Init(const DisplayConfig& config);

        /**
         * @brief 释放协议和传输层资源
         */
        void Release();

        bool IsInitialized() const {
            return mInitialized.load(std::memory_order_acquire);
        }
        bool IsUpgrading() const {
            return mUpgradeInProgress.load(std::memory_order_acquire);
        }

        /**
         * @brief 编码并发送一组写请求，等待全部帧收到屏端 ACK
         * @param requests 请求列表（地址 + 值），内部逐条编码为 0x82 写帧
         * @param timeoutMs 整体超时时间(毫秒)
         * @return 操作结果
         */
        ScreenResult SendRequests(const std::vector<Request>& requests, uint32_t timeoutMs);

        /**
         * @brief 读取 VP 寄存器（0x83 读命令，单帧应答）
         * @param addr 读取地址
         * @param readLen 读取字数
         * @param outData 应答数据（readLen 回显 + 数据字节）
         * @param timeoutMs 超时时间(毫秒)
         * @return 操作结果
         */
        ScreenResult ReadRegister(uint16_t addr, uint8_t readLen, std::vector<uint8_t>& outData, uint32_t timeoutMs);

        /**
         * @brief 设置触控事件回调（屏主动上传的 0x83 触控帧，读会话未命中部分）
         *
         * 注意：回调在引擎接收线程上下文执行，必须轻量（如仅做事件投递）——
         * 禁止在回调内调用本引擎的任何同步接口（SendRequests/ReadRegister
         * 等会等待应答，而应答处理在同一接收线程，必然死锁），也不可执行
         * 长耗时业务逻辑（会拖慢帧接收与会话超时判定）。
         */
        void SetTouchCallback(TouchCallback callback);

        /**
         * @brief 开关屏幕刷新与触控（0x00FC 接口：写 0x55AA 0x5A5A 停止 / 写 0x0000 0x0000 恢复）
         *
         * 该寄存器同时控制 DGUS 刷新与触控处理：升级流程先停触控，防触摸上传帧
         * 混入应答流干扰 ACK 配对；业务侧亦可用于会议进行中临时锁屏等场景
         * （停止后界面不再刷新，恢复时需重新下发页面数据）。
         */
        ScreenResult SetTouchEnabled(bool enabled, uint32_t timeoutMs = 2000);

        /**
         * @brief 串口固件升级
         *
         * 依次升级每个文件，全部完成后发一次重启命令使屏幕生效。
         * 升级期间上层应拒绝页面/控件操作（IsUpgrading）。
         *
         * 失败恢复：升级过程会停止 0x00FC（DGUS 刷新与触控），因此无论成功或
         * 失败都会收尾——成功走重启使新固件生效；失败则尝试恢复 0x00FC，
         * 避免屏幕停留在停止刷新状态（恢复帧也发不出去时只能外部重新上电）。
         * @param filePaths 固件文件路径列表（文件名前缀数字为库号）
         * @param timeoutMsPerOp 单次发送应答超时(毫秒)
         * @return 操作结果；升级阶段的失败（文件非法/Flash 写入/轮询超时等）
         *         统一返回 UpgradeFailed，具体失败阶段见日志；传输类失败
         *         保留 Timeout/TransportError
         */
        ScreenResult UpgradeFirmware(const std::vector<std::string>& filePaths, uint32_t timeoutMsPerOp = 3000);

    private:
        // ── 会话状态（引擎内部实现细节，不对外暴露）──
        enum class SessionState : uint8_t {
            Sending,     // 帧正在发送中
            WaitingAck,  // 帧已发送，等待协议层应答
            Completed    // 结果已交付等待方（成功或失败），会话即将销毁
        };

        /**
         * @brief 异步发送会话，跟踪请求的发送与协议应答
         *
         * 写请求：多帧，所有帧收到 ACK 后 promise 返回 OK；
         * 读请求：单帧，命中应答后 promise 返回 OK + 数据。
         */
        struct Session {
            uint64_t id = 0;
            SessionState state = SessionState::Sending;
            bool isRead = false;
            std::promise<std::pair<ScreenResult, std::vector<uint8_t>>> promise;

            // 写请求：多帧发送计数（sentFrames 与 mPendingAckQueue 条目一一对应）
            uint32_t totalFrames = 0;
            uint32_t sentFrames = 0;
            uint32_t ackedFrames = 0;

            // 读请求：应答匹配地址
            uint16_t expectedAddr = 0;
        };

        // ── 会话管理与传输 ──

        /**
         * @brief 待发送帧：编码结果 + 会话号 + 帧间间隔
         *
         * gapMs 由 Request 透传，用于屏端要求节流的控制类指令（如 0xB0 触控开关
         * 需每条间隔 20ms），由发送循环在帧之间执行等待。
         */
        struct OutgoingFrame {
            uint64_t sessionId = 0;
            std::vector<uint8_t> data {};
            uint16_t gapMs = 0;
        };

        ScreenResult SendFrames(std::vector<OutgoingFrame>&& frames, uint32_t timeoutMs);
        ScreenResult SendReadFrame(const std::vector<uint8_t>& frame, uint16_t expectedAddr,
                                   std::vector<uint8_t>& outData, uint32_t timeoutMs);
        void OnSendComplete(uint64_t sessionId, bool ok);
        void OnReceiveData(const uint8_t* data, size_t len);
        void HandleAckFrame();

        /**
         * @brief 会话超时后的应答流重同步
         *
         * 迪文写应答（0x82/0x4F4B）不含会话标识，只能按发送顺序 FIFO 配对；
         * 若仅丢弃超时会话而保留其"迟到应答"，该应答会顶掉后续会话的队首条目，
         * 造成计数永久错位。故超时即视为链路异常：整批在途会话一并以 reason
         * 结束（调用方可重试），并清空待应答队列与接收缓冲以恢复对齐。
         * 调用方须按「接收缓冲 → 会话表」锁序持有两把锁。
         */
        void ResetAckStateLocked(ScreenResult reason);

        /**
         * @brief 处理 0x83 帧的读应答匹配
         * @return 是否命中挂起的读会话（未命中可能为触控上传帧，由调用方继续上抛）
         */
        bool HandleReadAck(const Frame& frame);

        // ── 触控上抛 ──
        void DispatchTouchEvent(const Frame& frame);

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

        // ── 协议与传输层 ──
        std::unique_ptr<IScreenTransport> mTransport {};
        std::unique_ptr<IScreenProtocol> mProtocol {};

        // ── 会话状态 ──
        std::mutex mSessionMutex;
        std::unordered_map<uint64_t, std::unique_ptr<Session>> mSessions {};
        std::deque<uint64_t> mPendingAckQueue {};
        std::atomic<uint64_t> mNextSessionId {1};
        std::atomic<bool> mInitialized {false};

        // ── 接收缓冲 ──
        std::mutex mReceiveMutex;
        std::vector<uint8_t> mReceiveBuffer {};

        // ── 触控上抛回调 ──
        std::mutex mTouchMutex;
        TouchCallback mTouchCallback {};

        // ── 固件升级状态 ──
        std::atomic<bool> mUpgradeInProgress {false};
    };

}  // namespace qifeng

#endif  // HAL_SCREEN_SCREEN_HAL_H
