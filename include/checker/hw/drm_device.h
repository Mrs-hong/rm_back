/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <string>
#include <vector>

namespace qifeng::scm {

struct DisplayConfig;  // 前向声明，避免头文件循环依赖

/**
 * @brief 显示器信息
 */
struct DisplayInfo {
    std::string device;              // /dev/dri/cardN
    bool connected = false;          // 是否有显示器连接
    std::vector<std::string> notes;  // 附加说明
};

/**
 * @brief 探测显示器；内部根据 CHECKER_HAS_DRM 选择实现
 * @param cfg 显示器配置（DRI 路径、扫描上限等）
 * @return DisplayInfo 显示器信息
 */
DisplayInfo ProbeDisplay(const DisplayConfig &cfg);

}  // namespace qifeng::scm