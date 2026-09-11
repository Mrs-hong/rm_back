/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_TRANSPORT_SCREEN_TRANSPORT_H
#define HAL_SCREEN_TRANSPORT_SCREEN_TRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace qifeng {
    struct SendTask {
        uint64_t sessionId = 0;
        std::vector<uint8_t> data {};
    };

    /**
     * @brief 串口传输配置
     *
     * 放在传输接口头而非具体实现头：上层（ScreenEngine 的 DisplayConfig）
     * 需要按值持有传输配置，避免公共接口反向依赖具体传输实现。
     */
    struct SerialTransportConfig {
        std::string port = "/dev/ttyS2";
        uint32_t baudRate = 115200;
    };

    /**
     * @brief 屏幕传输层接口
     *
     * 支持异步发送与接收回调，发送完成和接收数据均通过回调通知上层
     * 注：接口统一采用 I 前缀（与 IScreenProtocol 一致）
     */
    class IScreenTransport {
    public:
        // 发送完成回调：sessionId 关联会话；ok=false 表示底层写入失败
        // （该帧不会到达屏幕，上层应立即以失败结束会话，而不是空等应答超时）
        using SendCompleteCallback = std::function<void(uint64_t sessionId, bool ok)>;
        using ReceiveCallback = std::function<void(const uint8_t* data, size_t len)>;

        IScreenTransport() = default;
        virtual ~IScreenTransport() = default;
        IScreenTransport(const IScreenTransport&) = delete;
        IScreenTransport& operator=(const IScreenTransport&) = delete;
        IScreenTransport(IScreenTransport&&) = delete;
        IScreenTransport& operator=(IScreenTransport&&) = delete;

        /**
         * @brief 初始化传输层
         * @return 初始化是否成功
         */
        virtual bool Init() = 0;

        /**
         * @brief 释放传输层资源
         */
        virtual void Release() = 0;

        /**
         * @brief 异步发送数据，入队后立即返回
         * @param sessionId 关联的会话ID
         * @param data 待发送数据
         * @return 是否成功入队
         */
        virtual bool Send(uint64_t sessionId, const std::vector<uint8_t>& data) = 0;

        virtual void SetSendCompleteCallback(SendCompleteCallback callback) = 0;
        virtual void SetReceiveCallback(ReceiveCallback callback) = 0;
    };

}  // namespace qifeng

#endif  // HAL_SCREEN_TRANSPORT_SCREEN_TRANSPORT_H
