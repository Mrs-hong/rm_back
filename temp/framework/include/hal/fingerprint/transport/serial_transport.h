/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_FINGERPRINT_TRANSPORT_SERIAL_TRANSPORT_H
#define HAL_FINGERPRINT_TRANSPORT_SERIAL_TRANSPORT_H

#include <atomic>
#include <cstdint>
#include <poll.h>
#include <string>
#include <termios.h>
#include <thread>
#include <vector>

#include "hal/bounded_queue.h"
#include "hal/fingerprint/transport/fingerprint_transport.h"

namespace qifeng {
    struct FpSendTask {
        uint64_t sessionId = 0;
        std::vector<uint8_t> data {};
    };

    struct FpSerialTransportConfig {
        std::string port = "/dev/ttyUSB0";
        uint32_t baudRate = 57600;
    };

    /**
     * @brief 指纹模组串口传输层实现
     *
     * 独立的收发线程 + 有界发送队列，避免阻塞调用方
     */
    class FpSerialTransport : public FingerprintTransport {
    public:
        explicit FpSerialTransport(const FpSerialTransportConfig& config);

        ~FpSerialTransport() override;

        FpSerialTransport(const FpSerialTransport&) = delete;
        FpSerialTransport& operator=(const FpSerialTransport&) = delete;
        FpSerialTransport(FpSerialTransport&&) = delete;
        FpSerialTransport& operator=(FpSerialTransport&&) = delete;

        bool Init() override;
        void Release() override;
        bool Send(uint64_t sessionId, const std::vector<uint8_t>& data) override;

        void SetSendCompleteCallback(SendCompleteCallback callback) override;
        void SetReceiveCallback(ReceiveCallback callback) override;
        void SetErrorCallback(ErrorCallback callback) override;

    private:
        void SendLoop();
        void ReceiveLoop();
        bool ConfigureSerial();
        bool BaudRate(speed_t& baud);
        bool WriteFrame(const FpSendTask& task);

        FpSerialTransportConfig mConfig;
        int mFd = -1;
        struct pollfd mPollFd {};
        std::atomic<bool> mRunning {false};

        BoundedQueue<FpSendTask> mSendQueue {64};
        SendCompleteCallback mSendCompleteCallback;
        ReceiveCallback mReceiveCallback;
        ErrorCallback mErrorCallback;

        std::thread mSendThread;
        std::thread mReceiveThread;
    };

}  // namespace qifeng

#endif  // HAL_FINGERPRINT_TRANSPORT_SERIAL_TRANSPORT_H
