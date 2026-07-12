/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/operation_recovery.h"

#include "qifeng_framework/common/logger.h"
#include "scmd/command_dispatcher.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"

#include <iostream>

namespace qifeng::scm {

    OperationRecoveryService::OperationRecoveryService(CommandDispatcher& dispatcher,
                                                       const ServiceContext& ctx,
                                                       KeyOperationRecorder& recorder)
        : mDispatcher(dispatcher), mContext(ctx), mRecorder(recorder) {
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    void OperationRecoveryService::Run() {
        KeyOperationRecord record;

        if (!mRecorder.LoadLastOperation(record)) {
            return;
        }

        SLOG_INFO << "Found last key operation: " << record.optName << " service=" << record.serviceName
                  << " tarDir=" << record.tarDir << " result=" << record.result;

        // result语义：0成功 1失败 2进行中（被异常终止）
        if (record.result == 0) {
            // 上次操作成功完成，直接清除记录
            SLOG_INFO << "Last operation completed successfully: " << record.optName;
            mRecorder.Clear();
            return;
        }

        if (record.result == 1) {
            // 上次操作已执行结束但失败，只做提示不做自动恢复或清理
            SLOG_WARN << "Last operation failed: " << record.optName << " for " << record.serviceName;
            std::cout << "[scmd] 上次操作失败: " << record.optName << " 服务: " << record.serviceName
                      << "，请手动检查后重新执行" << std::endl;
            return;
        }

        // result == 2：上次操作进行中被异常终止，尝试恢复
        SLOG_INFO << "Recovering incomplete operation: " << record.optName << " for " << record.serviceName;
        std::cout << "[scmd] 检测到未完成的操作: " << record.optName << " 服务: " << record.serviceName
                  << "，正在恢复..." << std::endl;

        // 由分发器根据 record.optName 查表分发到对应 handler 的 Recover()
        auto recoverResult = mDispatcher.Recover(record, mContext);

        if (recoverResult.IsDefalutSuccess()) {
            SLOG_INFO << "Recovery completed successfully for: " << record.optName;
            std::cout << "[scmd] 操作恢复成功: " << record.optName << std::endl;
            // 恢复成功后标记为已完成(result=0)，不立即清除记录
            // 这样即使本次恢复后再次崩溃，下次启动看到result=0会知道已恢复
            mRecorder.UpdateResult(0);
        } else {
            SLOG_WARN << "Recovery failed for " << record.optName << ": " << recoverResult.msg;
            std::cout << "[scmd] 操作恢复失败: " << record.optName << " - " << recoverResult.msg << std::endl;
            // 恢复失败时保留记录(result=2)，以便下次启动再次尝试恢复
        }
    }

}  // namespace qifeng::scm
