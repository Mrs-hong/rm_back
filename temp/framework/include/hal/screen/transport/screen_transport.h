/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_TRANSPORT_SCREEN_TRANSPORT_H
#define HAL_SCREEN_TRANSPORT_SCREEN_TRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace qifeng {
    struct SendTask {
        uint64_t sessionId = 0;
        std::vector<uint8_t> data {};
    };

    /**
     * @brief 屏幕传输层接口
     *
     * 支持异步发送与接收回调，发送完成和接收数据均通过回调通知上层
     */
    class ScreenTransport {
    public:
        using SendCompleteCallback = std::function<void(uint64_t sessionId)>;
        using ReceiveCallback = std::function<void(const uint8_t* data, size_t len)>;

        ScreenTransport() = default;
        virtual ~ScreenTransport() = default;
        ScreenTransport(const ScreenTransport&) = delete;
        ScreenTransport& operator=(const ScreenTransport&) = delete;
        ScreenTransport(ScreenTransport&&) = delete;
        ScreenTransport& operator=(ScreenTransport&&) = delete;

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
