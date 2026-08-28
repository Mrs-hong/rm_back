/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_TRANSPORT_SERIAL_TRANSPORT_H
#define HAL_SCREEN_TRANSPORT_SERIAL_TRANSPORT_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "hal/bounded_queue.h"
#include "hal/screen/transport/screen_transport.h"

namespace qifeng {
    struct SerialTransportConfig {
        std::string port = "/dev/ttyS2";
        uint32_t baudRate = 115200;
    };

    /**
     * @brief 串口传输层实现
     *
     * 独立的收发线程 + 有界发送队列，避免阻塞调用方
     */
    class SerialTransport : public ScreenTransport {
    public:
        explicit SerialTransport(const SerialTransportConfig& config);

        ~SerialTransport() override;

        SerialTransport(const SerialTransport&) = delete;
        SerialTransport& operator=(const SerialTransport&) = delete;
        SerialTransport(SerialTransport&&) = delete;
        SerialTransport& operator=(SerialTransport&&) = delete;

        bool Init() override;
        void Release() override;
        bool Send(uint64_t sessionId, const std::vector<uint8_t>& data) override;

        void SetSendCompleteCallback(SendCompleteCallback callback) override;
        void SetReceiveCallback(ReceiveCallback callback) override;

    private:
        void SendLoop();
        void ReceiveLoop();
        bool ConfigureSerial();

        SerialTransportConfig mConfig;
        int mFd = -1;
        std::atomic<bool> mRunning {false};

        BoundedQueue<SendTask> mSendQueue {64};
        SendCompleteCallback mSendCompleteCallback;
        ReceiveCallback mReceiveCallback;

        std::thread mSendThread;
        std::thread mReceiveThread;
    };

}  // namespace qifeng

#endif  // HAL_SCREEN_TRANSPORT_SERIAL_TRANSPORT_H
