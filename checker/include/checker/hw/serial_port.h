// =============================================================================
// serial_port.h — POSIX 串口封装（RAII）
//
// 构造时打开 tty 设备并配置波特率/8N1/原始模式；提供 write/read(限时)。
// 析构自动关闭 fd。打开失败时 is_open()==false，由调用方决定 Skipped。
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace checker {

class SerialPort {
 public:
  SerialPort() = default;
  // 打开设备并配置波特率；失败不抛异常，置 open_=false
  SerialPort(const std::string& device, int baud);
  ~SerialPort();

  SerialPort(const SerialPort&) = delete;
  SerialPort& operator=(const SerialPort&) = delete;

  bool is_open() const { return fd_ >= 0; }
  // 写入字节，返回实际写入字节数；<0 表示错误
  ssize_t write(const uint8_t* data, size_t len);
  // 限时读取：最多读 max_len 字节，超时 timeout_ms 毫秒；返回读取字节数
  ssize_t read(uint8_t* buf, size_t max_len, int timeout_ms);

 private:
  int fd_ = -1;
};

}  // namespace checker
