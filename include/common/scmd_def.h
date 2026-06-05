/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

namespace qifeng::scm {
    constexpr const char* DefaultConfigPath = "./.config/scmd.yaml";
    constexpr const char* DefaultServiceName = "service.yaml";
    constexpr const char* KeyOptFileName = "last_key_opt.yaml";  // 记录最后一次操作的执行状态、持久化文件、用于恢复
    constexpr const int DefaultMode = 0755;                      // 所有文件默认权限
}  // namespace qifeng::scm
