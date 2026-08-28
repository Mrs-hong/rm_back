//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_SUMMARY_CALLBACK_H
#define QIFENG_CA_INCLUDE_INTERNAL_SUMMARY_CALLBACK_H

#include <cstdint>
#include <string>

#include "schedule/task/summary_task.h"

namespace qifeng_ca {

    // 纪要结果回调处理: 接收纪要结果, 保存到Summary表, 通过WebSocket推送给客户端
    class SummaryCallback {
    public:
        // 处理纪要结果: 保存到Summary表 + WebSocket推送
        static void OnSummaryResult(const std::string &audioId, uint64_t accountId, const SummaryResult &result);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_SUMMARY_CALLBACK_H
