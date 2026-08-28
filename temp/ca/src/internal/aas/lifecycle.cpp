/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "qifeng_framework/aas/aas_callback.h"

#include "internal/aas/lifecycle.h"

namespace qifeng_ca {
    namespace aas {

        // 启动初始化
        bool InitAas() {
            return qifeng::aas::InitializeAasResources() == 0;
        }

        // 优雅退出
        bool ShutdownAas() {
            qifeng::aas::ReleaseAasResources();
            return true;
        }
    }  // namespace aas
}  // namespace qifeng_ca
