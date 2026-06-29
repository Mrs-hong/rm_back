// =============================================================================
// gpio.cpp — sysfs GPIO 封装实现
// =============================================================================
#include "checker/hw/gpio.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "checker/log/logger.hpp"

namespace checker {

namespace {
bool write_str(const std::string& path, const std::string& value) {
  int fd = ::open(path.c_str(), O_WRONLY);
  if (fd < 0) return false;
  ssize_t n = ::write(fd, value.c_str(), value.size());
  ::close(fd);
  return n == (ssize_t)value.size();
}

bool read_str(const std::string& path, std::string& value) {
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) return false;
  char buf[16] = {0};
  ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
  ::close(fd);
  if (n <= 0) return false;
  value.assign(buf, n);
  return true;
}
}  // namespace

bool Gpio::write_file(const std::string& path, const std::string& value) { return write_str(path, value); }
bool Gpio::read_file(const std::string& path, std::string& value) { return read_str(path, value); }

Gpio::Gpio(const std::string& number) : number_(number) {
  // export（若已导出，写失败可忽略）
  write_str("/sys/class/gpio/export", number_);
  base_ = "/sys/class/gpio/gpio" + number_;
  if (!write_str(base_ + "/direction", "out")) {
    LOG_DEBUG("gpio %s set direction failed: %s", number_.c_str(), strerror(errno));
    ok_ = false;
    return;
  }
  ok_ = true;
}

Gpio::~Gpio() {
  // unexport（忽略错误）
  if (!number_.empty()) write_str("/sys/class/gpio/unexport", number_);
}

bool Gpio::set(bool high) {
  if (!ok_) return false;
  return write_str(base_ + "/value", high ? "1" : "0");
}

bool Gpio::get(bool& high) {
  if (!ok_) return false;
  std::string v;
  if (!read_str(base_ + "/value", v)) return false;
  high = (v.find('1') != std::string::npos);
  return true;
}

}  // namespace checker
