// =============================================================================
// fingerprint_checker.cpp — 指纹模组自检
//
// 通过串口与模组握手。握手包为通用示意（EF01 帧头 + 取模组特征命令），
// 实际部署时按模组协议替换 cmd/校验逻辑。无设备则 Skipped。
// =============================================================================
#include "checker/checkers/fingerprint_checker.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "checker/hw/serial_port.h"
#include "checker/log/logger.hpp"
#include "checker/util/time_util.h"

namespace checker {

CheckResult FingerprintChecker::run(const Context& ctx) {
  CheckResult r(name());
  Timer timer;

  const auto& cfg = ctx.config.fingerprint;
  SerialPort sp(cfg.device, cfg.baud);
  if (!sp.is_open()) {
    // 设备节点缺失：视为未装配，跳过
    r.status = Status::kSkipped;
    r.message = "device not present: " + cfg.device;
    r.elapsed_ms = timer.elapsed_ms();
    LOG_INFO("[fingerprint] %s", r.message.c_str());
    return r;
  }

  // 握手包示意（常见的指纹模组读指纹特征命令）
  // 实际部署请按模组 datasheet 替换
  const uint8_t cmd[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                         0x01, 0x00, 0x03, 0x01, 0x00, 0x05};
  sp.write(cmd, sizeof(cmd));

  // 限时读响应
  uint8_t buf[32] = {0};
  ssize_t n = sp.read(buf, sizeof(buf), cfg.timeout_ms);
  r.details.emplace_back("resp_len", std::to_string(n > 0 ? n : 0));

  // 应答中通常含状态码 0x00(成功) 或 0x07(无手指)；只要回包即认为模组在线
  bool alive = (n > 0) && (memchr(buf, 0x00, n) || memchr(buf, 0x07, n));
  r.status = alive ? Status::kPass : Status::kWarning;
  r.message = alive ? "fingerprint module alive" : "no valid response";
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[fingerprint] %s (%dms)", r.message.c_str(), r.elapsed_ms);
  return r;
}

}  // namespace checker
