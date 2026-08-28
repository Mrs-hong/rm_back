/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#ifndef QIFENG_CA_INCLUDE_INTERNAL_HAL_LIFECYCLE_H
#define QIFENG_CA_INCLUDE_INTERNAL_HAL_LIFECYCLE_H

namespace qifeng_ca {
    namespace hal {

        // 启动初始化
        bool InitHal();

        // 优雅退出
        bool ShutdownHal();
    }  // namespace hal
}  // namespace qifeng_ca
#endif  // QIFENG_CA_INCLUDE_INTERNAL_HAL_LIFECYCLE_H
