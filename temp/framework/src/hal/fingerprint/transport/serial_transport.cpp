/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include "common/logger.h"
#include "hal/fingerprint/transport/serial_transport.h"

namespace qifeng {
    FpSerialTransport::FpSerialTransport(const FpSerialTransportConfig& config) : mConfig(config) {
    }

    FpSerialTransport::~FpSerialTransport() {
        Release();
    }

    bool FpSerialTransport::BaudRate(speed_t& baud) {
        switch (mConfig.baudRate) {
            case 9600: baud = B9600; break;
            case 19200: baud = B19200; break;
            case 38400: baud = B38400; break;
            case 57600: baud = B57600; break;
            case 115200: baud = B115200; break;
            case 230400: baud = B230400; break;
            case 460800: baud = B460800; break;
            case 921600: baud = B921600; break;
            default:
                SLOG_ERROR << "FpSerial: unsupported baud rate: " << mConfig.baudRate;
                return false;
        }
        return true;
    }

    bool FpSerialTransport::ConfigureSerial() {
        struct termios options {};
        if (tcgetattr(mFd, &options) != 0) {
            SLOG_ERROR << "FpSerial: tcgetattr failed: " << strerror(errno);
            return false;
        }

        speed_t baud = B115200;
        if (!BaudRate(baud)) {
            return false;
        }
        cfsetispeed(&options, baud);
        cfsetospeed(&options, baud);

        options.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
        options.c_cflag &= static_cast<tcflag_t>(~PARENB);
        options.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
        options.c_cflag &= static_cast<tcflag_t>(~CSIZE);
        options.c_cflag |= static_cast<tcflag_t>(CS8);
        options.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);

        options.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ECHOE | ECHOCTL | ECHOK | ECHOKE | ISIG));
        options.c_oflag &= static_cast<tcflag_t>(~OPOST);
        options.c_iflag &= static_cast<tcflag_t>(~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL));

        options.c_cc[VMIN] = 1;
        options.c_cc[VTIME] = 0;

        if (tcsetattr(mFd, TCSANOW, &options) != 0) {
            SLOG_ERROR << "FpSerial: tcsetattr failed: " << strerror(errno);
            return false;
        }

        tcflush(mFd, TCIOFLUSH);
        return true;
    }

    bool FpSerialTransport::Init() {
        mFd = open(mConfig.port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);  // NOLINT(cppcoreguidelines-pro-type-vararg)
        if (mFd < 0) {
            SLOG_ERROR << "FpSerial: failed to open " << mConfig.port << ": " << strerror(errno);
            return false;
        }

        int flags = fcntl(mFd, F_GETFL, 0);  // NOLINT(cppcoreguidelines-pro-type-vararg)
        fcntl(mFd, F_SETFL, flags & ~O_NONBLOCK);  // NOLINT(cppcoreguidelines-pro-type-vararg)

        if (!ConfigureSerial()) {
            close(mFd);
            mFd = -1;
            return false;
        }

        mRunning.store(true, std::memory_order_release);

        mPollFd.fd = mFd;
        mPollFd.events = POLLIN;

        mSendThread = std::thread(&FpSerialTransport::SendLoop, this);
        mReceiveThread = std::thread(&FpSerialTransport::ReceiveLoop, this);

        SLOG_INFO << "FpSerial initialized: " << mConfig.port << " @ " << mConfig.baudRate;
        return true;
    }

    void FpSerialTransport::Release() {
        if (!mRunning.load(std::memory_order_acquire)) {
            return;
        }

        mRunning.store(false, std::memory_order_release);
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

        SLOG_INFO << "FpSerial released";
    }

    bool FpSerialTransport::Send(uint64_t sessionId, const std::vector<uint8_t>& data) {
        FpSendTask task;
        task.sessionId = sessionId;
        task.data = data;
        return mSendQueue.Push(std::move(task));
    }

    void FpSerialTransport::SetSendCompleteCallback(SendCompleteCallback callback) {
        mSendCompleteCallback = std::move(callback);
    }

    void FpSerialTransport::SetReceiveCallback(ReceiveCallback callback) {
        mReceiveCallback = std::move(callback);
    }

    void FpSerialTransport::SetErrorCallback(ErrorCallback callback) {
        mErrorCallback = std::move(callback);
    }

    bool FpSerialTransport::WriteFrame(const FpSendTask& task) {
        size_t totalLen = task.data.size();
        size_t offset = 0;
        while (offset < totalLen) {
            ssize_t written = write(mFd, task.data.data() + offset, totalLen - offset);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                SLOG_ERROR << "FpSerial: write failed: " << strerror(errno);
                return false;
            }
            offset += static_cast<size_t>(written);
        }
        tcdrain(mFd);
        return true;
    }

    void FpSerialTransport::SendLoop() {
        while (mRunning.load(std::memory_order_acquire)) {
            FpSendTask task;
            if (!mSendQueue.WaitPop(task)) {
                break;
            }
            if (!mRunning.load(std::memory_order_acquire)) {
                break;
            }

            bool success = WriteFrame(task);
            if (success) {
                SLOG_DEBUG << "FpSerial: sent " << task.data.size()
                           << " bytes for session " << task.sessionId;
                if (mSendCompleteCallback) {
                    mSendCompleteCallback(task.sessionId);
                }
            }
        }
    }

    void FpSerialTransport::ReceiveLoop() {  // NOLINT(readability-function-cognitive-complexity)
        std::array<uint8_t, 256> buf {};

        while (mRunning.load(std::memory_order_acquire)) {
            constexpr int kPollTimeoutMs = 100;
            int ret = poll(&mPollFd, 1, kPollTimeoutMs);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                SLOG_ERROR << "FpSerial: poll failed: " << strerror(errno);
                break;
            }

            if (ret == 0) {
                continue;
            }

            if (mPollFd.revents & static_cast<short>(POLLERR | POLLHUP | POLLNVAL)) {
                SLOG_ERROR << "FpSerial: port error detected";
                break;
            }

            if (mPollFd.revents & POLLIN) {
                ssize_t bytesRead = read(mFd, buf.data(), buf.size());
                if (bytesRead < 0) {
                    if (errno == EINTR || errno == EAGAIN) {
                        continue;
                    }
                    SLOG_ERROR << "FpSerial: read failed: " << strerror(errno);
                    break;
                }

                if (bytesRead > 0 && mReceiveCallback) {
                    mReceiveCallback(buf.data(), static_cast<size_t>(bytesRead));
                }
            }
        }

        if (mRunning.load(std::memory_order_acquire) && mErrorCallback) {
            mErrorCallback();
        }
    }

}  // namespace qifeng
