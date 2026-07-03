/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>

namespace qifeng {
    namespace scm {

        struct VersionInfo {
            std::string version;    // 版本号，如 "0.0.1"
            std::string buildTime;  // 编译时间，如 "2026-05-27 12:00:00"
            std::string gitCommit;  // Git提交 hash
        };

        const VersionInfo &GetVersionInfo();

    }  // namespace scm
}  // namespace qifeng
