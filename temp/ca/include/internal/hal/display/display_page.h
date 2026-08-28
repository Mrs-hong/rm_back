//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_DISPLAY_PAGE_H
#define QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_DISPLAY_PAGE_H

#include <cstdint>

namespace qifeng_ca {

    // 显示屏当前页面状态
    enum class DisplayPage : uint8_t {
        Idle = 0,        // 待机页面
        Meeting,         // 会议页面
        Fingerprint,     // 指纹录入页面
        Upgrade          // 升级页面
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_DISPLAY_PAGE_H
