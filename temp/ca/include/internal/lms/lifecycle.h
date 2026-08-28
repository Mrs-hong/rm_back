/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#ifndef QIFENG_CA_INCLUDE_INTERNAL_LMS_LIFECYCLE_H
#define QIFENG_CA_INCLUDE_INTERNAL_LMS_LIFECYCLE_H

#include <atomic>

namespace qifeng_ca {
    namespace lms {

        // 启动初始化
        bool InitLms();

        // 优雅退出
        bool ShutdownLms();

        // LMS初始化状态查询(全局原子, main中异步初始化完成后置为true)
        // SummaryTask在Start时会等待此状态, 避免在LMS未就绪时调用
        bool IsLmsReady();

        // 内部接口: 仅由lifecycle.cpp在异步初始化成功/失败时设置
        void SetLmsReady(bool ready);

    }  // namespace lms
}  // namespace qifeng_ca
#endif  // QIFENG_CA_INCLUDE_INTERNAL_LMS_LIFECYCLE_H
