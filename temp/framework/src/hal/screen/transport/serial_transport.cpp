/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

#include "common/logger.h"
#include "hal/screen/transport/serial_transport.h"

namespace qifeng {
    SerialTransport::SerialTransport(const SerialTransportConfig& config) : mConfig(config) {
    }

    SerialTransport::~SerialTransport() {
        Release();
    }

    bool SerialTransport::ConfigureSerial() {
        struct termios options {};
        if (tcgetattr(mFd, &options) != 0) {
            SLOG_ERROR << "tcgetattr failed: " << strerror(errno);
            return false;
        }

        speed_t baud = B115200;
        switch (mConfig.baudRate) {
            case 9600:
                baud = B9600;
                break;
            case 19200:
                baud = B19200;
                break;
            case 38400:
                baud = B38400;
                break;
            case 57600:
                baud = B57600;
                break;
            case 115200:
                baud = B115200;
                break;
            case 230400:
                baud = B230400;
                break;
            case 460800:
                baud = B460800;
                break;
            case 921600:
                baud = B921600;
                break;
            default:
                SLOG_ERROR << "Unsupported baud rate: " << mConfig.baudRate;
                return false;
        }

        cfsetispeed(&options, baud);
        cfsetospeed(&options, baud);

        // 8N1: 8数据位、无校验、1停止位，禁用硬件流控
        options.c_cflag |= (CLOCAL | CREAD);
        options.c_cflag &= ~PARENB;
        options.c_cflag &= ~CSTOPB;
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;
        options.c_cflag &= ~CRTSCTS;

        // 原始模式：禁用回显、规范模式和所有软件流控
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOCTL | ECHOK | ECHOKE | ISIG);
        options.c_oflag &= ~OPOST;
        options.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

        options.c_cc[VMIN] = 1;
        options.c_cc[VTIME] = 0;

        if (tcsetattr(mFd, TCSANOW, &options) != 0) {
            SLOG_ERROR << "tcsetattr failed: " << strerror(errno);
            return false;
        }

        tcflush(mFd, TCIOFLUSH);
        return true;
    }

    bool SerialTransport::Init() {
        // 先以非阻塞方式打开，再切换为阻塞模式，避免open时卡住
        mFd = open(mConfig.port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (mFd < 0) {
            SLOG_ERROR << "Failed to open serial port " << mConfig.port << ": " << strerror(errno);
            return false;
        }

        int flags = fcntl(mFd, F_GETFL, 0);
        fcntl(mFd, F_SETFL, flags & ~O_NONBLOCK);

        if (!ConfigureSerial()) {
            close(mFd);
            mFd = -1;
            return false;
        }

        mRunning.store(true, std::memory_order_release);

        mSendThread = std::thread(&SerialTransport::SendLoop, this);
        mReceiveThread = std::thread(&SerialTransport::ReceiveLoop, this);

        SLOG_INFO << "SerialTransport initialized: " << mConfig.port << " @ " << mConfig.baudRate;
        return true;
    }

    void SerialTransport::Release() {
        if (!mRunning.load(std::memory_order_acquire)) {
            return;
        }

        mRunning.store(false, std::memory_order_release);
        // 停止队列以唤醒阻塞在WaitPop上的发送线程
        mSendQueue.Stop();

        if (mSendThread.joinable()) {
            mSendThread.join();
        }
        if (mReceiveThread.joinable()) {
            mReceiveThread.join();
        }

        if (mFd >= 0) {
            close(mFd);
            mFd = -1;
        }

        SLOG_INFO << "SerialTransport released";
    }

    bool SerialTransport::Send(uint64_t sessionId, const std::vector<uint8_t>& data) {
        SendTask task;
        task.sessionId = sessionId;
        task.data = data;
        return mSendQueue.Push(std::move(task));
    }

    void SerialTransport::SetSendCompleteCallback(SendCompleteCallback callback) {
        mSendCompleteCallback = std::move(callback);
    }

    void SerialTransport::SetReceiveCallback(ReceiveCallback callback) {
        mReceiveCallback = std::move(callback);
    }

    void SerialTransport::SendLoop() {
        while (mRunning.load(std::memory_order_acquire)) {
            SendTask task;
            if (!mSendQueue.WaitPop(task)) {
                break;
            }

            if (!mRunning.load(std::memory_order_acquire)) {
                break;
            }

            ssize_t written = write(mFd, task.data.data(), task.data.size());
            if (written < 0) {
                SLOG_ERROR << "Serial write failed: " << strerror(errno);
            } else if (static_cast<size_t>(written) != task.data.size()) {
                SLOG_WARN << "Serial partial write: " << written << "/" << task.data.size();
            } else {
                SLOG_INFO << "Serial sent " << written << " bytes for session " << task.sessionId;
            }

            if (mSendCompleteCallback) {
                mSendCompleteCallback(task.sessionId);
            }
        }
    }

    void SerialTransport::ReceiveLoop() {
        constexpr int kPollTimeoutMs = 100;
        uint8_t buf[256];

        while (mRunning.load(std::memory_order_acquire)) {
            struct pollfd pfd {};
            pfd.fd = mFd;
            pfd.events = POLLIN;

            int ret = poll(&pfd, 1, kPollTimeoutMs);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                SLOG_ERROR << "poll failed: " << strerror(errno);
                break;
            }

            if (ret == 0) {
                continue;
            }

            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                SLOG_ERROR << "Serial port error detected";
                break;
            }

            if (pfd.revents & POLLIN) {
                ssize_t bytesRead = read(mFd, buf, sizeof(buf));
                if (bytesRead < 0) {
                    if (errno == EINTR || errno == EAGAIN) {
                        continue;
                    }
                    SLOG_ERROR << "Serial read failed: " << strerror(errno);
                    break;
                }

                if (bytesRead > 0 && mReceiveCallback) {
                    mReceiveCallback(buf, static_cast<size_t>(bytesRead));
                }
            }
        }
    }

}  // namespace qifeng
