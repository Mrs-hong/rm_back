/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/hw/serial_port.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

namespace {

/**
 * @brief 将常用波特率转为系统常量
 */
speed_t BaudToSpeed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        default:     return B57600;
    }
}

}  // namespace

SerialPort::SerialPort(const std::string &device, int baud) {
    // c-style vararg open() 是 POSIX 标准接口，无法避免
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvarargs"
    mFd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);  // NOLINT(cppcoreguidelines-pro-type-vararg)
#pragma GCC diagnostic pop
    if (mFd < 0) {
        SLOG_DEBUG << "serial open " << device << " failed: " << strerror(errno);
        return;
    }

    struct termios tio {};
    tcgetattr(mFd, &tio);
    cfsetispeed(&tio, BaudToSpeed(baud));
    cfsetospeed(&tio, BaudToSpeed(baud));
    // 8N1，无流控，原始模式
    tio.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
    tio.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    tio.c_cflag |= static_cast<tcflag_t>(CS8);
    tio.c_cflag &= static_cast<tcflag_t>(~PARENB);
    tio.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
    tio.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ECHOE | ISIG));
    tio.c_iflag &= static_cast<tcflag_t>(~(IXON | IXOFF | IXANY | IGNBRK | INLCR | ICRNL));
    tio.c_oflag &= static_cast<tcflag_t>(~OPOST);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    tcsetattr(mFd, TCSANOW, &tio);
    tcflush(mFd, TCIOFLUSH);
}

SerialPort::~SerialPort() {
    if (mFd >= 0) {
        ::close(mFd);
    }
}

ssize_t SerialPort::Write(const uint8_t *data, size_t len) {
    if (mFd < 0) {
        return -1;
    }
    return ::write(mFd, data, len);
}

ssize_t SerialPort::Read(uint8_t *buf, size_t max_len, int timeout_ms) {
    if (mFd < 0) {
        return -1;
    }
    // 用 select 等待数据，带超时
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(mFd, &rfds);
    struct timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = static_cast<__suseconds_t>(timeout_ms % 1000) * 1000;
    int ret = ::select(mFd + 1, &rfds, nullptr, nullptr, &tv);
    if (ret <= 0) {
        return 0;  // 超时或出错
    }
    return ::read(mFd, buf, max_len);
}

}  // namespace qifeng::scm