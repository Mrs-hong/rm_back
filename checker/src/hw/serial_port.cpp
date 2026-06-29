// =============================================================================
// serial_port.cpp — POSIX 串口封装实现
// =============================================================================
#include "checker/hw/serial_port.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "checker/log/logger.hpp"

namespace checker {

namespace {

// 将常用波特率转为系统常量
speed_t baud_to_speed(int baud) {
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

SerialPort::SerialPort(const std::string& device, int baud) {
  fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) {
    LOG_DEBUG("serial open %s failed: %s", device.c_str(), strerror(errno));
    return;
  }

  struct termios tio {};
  tcgetattr(fd_, &tio);
  cfsetispeed(&tio, baud_to_speed(baud));
  cfsetospeed(&tio, baud_to_speed(baud));
  // 8N1，无流控，原始模式
  tio.c_cflag |= (CLOCAL | CREAD);
  tio.c_cflag &= ~CSIZE;
  tio.c_cflag |= CS8;
  tio.c_cflag &= ~PARENB;
  tio.c_cflag &= ~CSTOPB;
  tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  tio.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | INLCR | ICRNL);
  tio.c_oflag &= ~OPOST;
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;
  tcsetattr(fd_, TCSANOW, &tio);
  tcflush(fd_, TCIOFLUSH);
}

SerialPort::~SerialPort() {
  if (fd_ >= 0) ::close(fd_);
}

ssize_t SerialPort::write(const uint8_t* data, size_t len) {
  if (fd_ < 0) return -1;
  return ::write(fd_, data, len);
}

ssize_t SerialPort::read(uint8_t* buf, size_t max_len, int timeout_ms) {
  if (fd_ < 0) return -1;
  // 用 select 等待数据，带超时
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(fd_, &rfds);
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  int ret = ::select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
  if (ret <= 0) return 0;  // 超时或出错
  return ::read(fd_, buf, max_len);
}

}  // namespace checker
