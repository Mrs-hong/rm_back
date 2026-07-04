/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/hw/drm_device.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "checker/checkers/display_checker.h"  // DisplayConfig 定义

#if defined(CHECKER_HAS_DRM) && CHECKER_HAS_DRM
    #include <xf86drm.h>
    #include <xf86drmMode.h>
#endif

namespace qifeng::scm {

#if defined(CHECKER_HAS_DRM) && CHECKER_HAS_DRM
    // libdrm 实现：找到第一个含 connected connector 的 card
    static DisplayInfo probe_with_drm(const DisplayConfig &cfg) {
        DisplayInfo info;
        for (int card = 0; card < cfg.max_card_index; ++card) {
            std::string path = cfg.dri_path + "/card" + std::to_string(card);
            int fd = ::open(path.c_str(), O_RDWR);
            if (fd < 0) {
                continue;
            }
            drmModeRes* res = drmModeGetResources(fd);
            if (res) {
                for (int i = 0; i < res->count_connectors; ++i) {
                    drmModeConnector* c = drmModeGetConnector(fd, res->connectors[i]);
                    if (c) {
                        if (c->connection == DRM_MODE_CONNECTED) {
                            info.device = path;
                            info.connected = true;
                            info.notes.emplace_back("connector_id=" + std::to_string(c->connector_id));
                        }
                        drmModeFreeConnector(c);
                        if (info.connected) {
                            break;
                        }
                    }
                    drmModeFreeResources(res);
                }
                ::close(fd);
                if (info.connected)
                    break;
                if (info.device.empty())
                    info.device = path;  // 记录第一个存在的 card
            }
            return info;
        }
#else
    // 降级实现：检查 DRI 目录与 /sys/class/drm 节点
    static DisplayInfo ProbeWithSysfs(const DisplayConfig &cfg) {
        DisplayInfo info;
        DIR* d = ::opendir(cfg.dri_path.c_str());
        if (!d) {
            info.notes.emplace_back("no " + cfg.dri_path);
            return info;
        }
        struct dirent* e = nullptr;
        while ((e = ::readdir(d)) != nullptr) {
            if (strncmp(e->d_name, "card", 4) != 0) {
                continue;
            }
            info.device = cfg.dri_path + "/" + e->d_name;
            break;
        }
        ::closedir(d);
        // /sys/class/drm/cardN-connector-status 在某些设备可读
        DIR* sd = ::opendir("/sys/class/drm");
        if (sd) {
            while ((e = ::readdir(sd)) != nullptr) {
                if (strstr(e->d_name, "-") == nullptr) {
                    continue;
                }
                info.notes.emplace_back("drm_node=" + std::string(e->d_name));
            }
            ::closedir(sd);
        }
        // 无法确切判断 connected，标记为未知
        info.connected = !info.device.empty();
        if (info.device.empty()) {
            info.notes.emplace_back("no dri card found");
        }
        return info;
    }
#endif

        DisplayInfo ProbeDisplay(const DisplayConfig &cfg) {
#if defined(CHECKER_HAS_DRM) && CHECKER_HAS_DRM
            return probe_with_drm(cfg);
#else
        return ProbeWithSysfs(cfg);
#endif
        }

    }  // namespace qifeng::scm