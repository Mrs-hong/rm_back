/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_INTERNAL_AAS_LIFECYCLE_H
#define QIFENG_CA_INCLUDE_INTERNAL_AAS_LIFECYCLE_H

namespace qifeng_ca {
    namespace aas {

        // 启动初始化
        bool InitAas();

        // 优雅退出
        bool ShutdownAas();
    }  // namespace aas
}  // namespace qifeng_ca
#endif  // QIFENG_CA_INCLUDE_INTERNAL_AAS_LIFECYCLE_H
