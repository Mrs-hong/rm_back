// =============================================================================
// gpio.h — sysfs GPIO 封装（RAII）
//
// 构造时 export 指定引脚并设为输出；提供 set()/get()。析构 unexport。
// x86 无 sysfs gpio 子系统时构造失败，由调用方决定 Skipped。
// =============================================================================
#pragma once

#include <string>

namespace checker {

class Gpio {
 public:
  Gpio() = default;
  // 导出引脚并设为 out；失败则 is_open()==false
  explicit Gpio(const std::string& number);
  ~Gpio();

  Gpio(const Gpio&) = delete;
  Gpio& operator=(const Gpio&) = delete;

  bool is_open() const { return ok_; }
  // 设置电平；成功返回 true
  bool set(bool high);
  // 回读当前电平
  bool get(bool& high);

 private:
  static bool write_file(const std::string& path, const std::string& value);
  static bool read_file(const std::string& path, std::string& value);

  std::string number_;
  std::string base_;  // /sys/class/gpio/gpioN
  bool ok_ = false;
};

}  // namespace checker
