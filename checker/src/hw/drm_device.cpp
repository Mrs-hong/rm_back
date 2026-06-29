// =============================================================================
// drm_device.cpp — 显示器探测
//   有 libdrm：枚举 connector 状态
//   无 libdrm：降级为 /dev/dri 与 /sys/class/drm 节点存在性检查
// =============================================================================
#include "checker/hw/drm_device.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "checker/log/logger.hpp"

#if defined(CHECKER_HAS_DRM) && CHECKER_HAS_DRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

namespace checker {

#if defined(CHECKER_HAS_DRM) && CHECKER_HAS_DRM
// libdrm 实现：找到第一个含 connected connector 的 card
static DisplayInfo probe_with_drm() {
  DisplayInfo info;
  for (int card = 0; card < 8; ++card) {
    std::string path = "/dev/dri/card" + std::to_string(card);
    int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) continue;
    drmModeRes* res = drmModeGetResources(fd);
    if (res) {
      for (int i = 0; i < res->count_connectors; ++i) {
        drmModeConnector* c = drmModeGetConnector(fd, res->connectors[i]);
        if (c) {
          if (c->connection == DRM_MODE_CONNECTED) {
            info.device = path;
            info.connected = true;
            info.notes.push_back("connector_id=" + std::to_string(c->connector_id));
          }
          drmModeFreeConnector(c);
          if (info.connected) break;
        }
      }
      drmModeFreeResources(res);
    }
    ::close(fd);
    if (info.connected) break;
    if (info.device.empty()) info.device = path;  // 记录第一个存在的 card
  }
  return info;
}
#else
// 降级实现：检查 /dev/dri 与 /sys/class/drm 节点
static DisplayInfo probe_with_sysfs() {
  DisplayInfo info;
  DIR* d = ::opendir("/dev/dri");
  if (!d) {
    info.notes.push_back("no /dev/dri");
    return info;
  }
  struct dirent* e;
  while ((e = ::readdir(d)) != nullptr) {
    if (strncmp(e->d_name, "card", 4) != 0) continue;
    info.device = std::string("/dev/dri/") + e->d_name;
    break;
  }
  ::closedir(d);
  // /sys/class/drm/cardN-connector-status 在某些设备可读
  DIR* sd = ::opendir("/sys/class/drm");
  if (sd) {
    while ((e = ::readdir(sd)) != nullptr) {
      if (strstr(e->d_name, "-") == nullptr) continue;
      info.notes.push_back(std::string("drm_node=") + e->d_name);
    }
    ::closedir(sd);
  }
  // 无法确切判断 connected，标记为未知
  info.connected = !info.device.empty();
  if (info.device.empty()) info.notes.push_back("no dri card found");
  return info;
}
#endif

DisplayInfo probe_display() {
#if defined(CHECKER_HAS_DRM) && CHECKER_HAS_DRM
  return probe_with_drm();
#else
  return probe_with_sysfs();
#endif
}

}  // namespace checker
