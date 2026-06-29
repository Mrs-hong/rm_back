// =============================================================================
// drm_device.h — libdrm 显示器探测（条件编译）
//
// 有 libdrm (CHECKER_HAS_DRM=1)：枚举 /dev/dri/card*，查找 connected connector。
// 无 libdrm：降级为 /dev/dri 与 /sys/class/drm 节点存在性检查。
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace checker {

struct DisplayInfo {
  std::string device;          // /dev/dri/cardN
  bool connected = false;      // 是否有显示器连接
  std::vector<std::string> notes;  // 附加说明
};

// 探测显示器；内部根据 CHECKER_HAS_DRM 选择实现
DisplayInfo probe_display();

}  // namespace checker
