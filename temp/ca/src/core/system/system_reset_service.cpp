//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"

#include "common/common.h"
#include "common/service_readiness.h"
#include "core/system/system_reset_service.h"
#include "dao_managers/user_dao_manager.h"
#include "internal/recording_manager.h"

namespace qifeng_ca {

    SystemResetService::SystemResetService() {
        RegisterDefaultTasks();
    }

    SystemResetService &SystemResetService::GetInstance() {
        static SystemResetService Instance;
        return Instance;
    }

    void SystemResetService::RegisterTask(std::unique_ptr<ResetTask> task) {
        if (task) {
            SLOG_INFO << "ResetTask registered: " << task->Name();
            mTasks.push_back(std::move(task));
        }
    }

    Status SystemResetService::ResetSystem(uint64_t operatorAccountId) {
        if (operatorAccountId == 0) {
            return Status {-1, "无效的操作者账户"};
        }
        models::User op = UserDaoManager::GetInstance().GetByAccountId(operatorAccountId);
        if (op.mAccountId == 0) {
            return Status {-1, "操作者账户不存在"};
        }
        if (op.mGroupId != Authority::ADMINISTRATOR) {
            return Status {-1, "无操作权限"};
        }

        // 录音状态校验: 有正在进行的录音时禁止重置, 避免录音数据丢失
        if (RecordingManager::GetInstance().IsRecording()) {
            return Status {-1, "当前有正在进行的录音, 不能重置系统"};
        }

        // 重置开始: 切换服务状态为 ResetSystem, 完成后恢复 Ready
        ServiceReadiness::GetInstance().SetStatus(ServiceReadiness::ServiceStatus::ResetSystem);
        ScopeExit releaseGuard([]() { ServiceReadiness::GetInstance().MarkReady(); });

        SLOG_INFO << "SystemReset: start, operator=" << operatorAccountId << ", tasks=" << mTasks.size();
        Status lastError;
        for (const auto &task : mTasks) {
            SLOG_INFO << "SystemReset: running task [" << task->Name() << "]";
            auto status = task->Execute(operatorAccountId);
            if (!status.IsSuccess()) {
                SLOG_ERROR << "SystemReset: task [" << task->Name() << "] failed, " << status.ToString();
                lastError = status;
            }
        }
        SLOG_INFO << "SystemReset: completed";
        return lastError.IsSuccess() ? Status {} : Status {-1, "系统重置部分失败"};
    }

    // 按注册顺序执行任务
    void SystemResetService::RegisterDefaultTasks() {
        RegisterTask(CreateDeleteUsersResetTask());
        RegisterTask(CreateDeleteFingerprintsResetTask());
        RegisterTask(CreateResetTablesResetTask());
        RegisterTask(CreateCleanupFilesResetTask());
    }

}  // namespace qifeng_ca
