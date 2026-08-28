//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_SYSTEM_RESET_TASK_H
#define QIFENG_CA_INCLUDE_CORE_SYSTEM_RESET_TASK_H

#include <cstdint>
#include <memory>
#include <string_view>

#include "common/status.h"

namespace qifeng_ca {

    // 单个重置任务抽象接口: 每个子类实现一个独立的重置步骤
    class ResetTask {
    public:
        virtual ~ResetTask() = default;
        ResetTask() = default;
        ResetTask(const ResetTask &) = delete;
        ResetTask &operator=(const ResetTask &) = delete;
        ResetTask(ResetTask &&) = delete;
        ResetTask &operator=(ResetTask &&) = delete;

        // 任务名称(用于日志与状态汇报)
        virtual std::string_view Name() const = 0;

        // 执行重置, operatorAccountId 为发起重置的管理员账户ID
        virtual Status Execute(uint64_t operatorAccountId) = 0;
    };

    // 删除group=2普通用户, 重置admin密码, 清理group=1/3默认账户的音频数据(保留账号)
    std::unique_ptr<ResetTask> CreateDeleteUsersResetTask();

    // 删除设备上1-100号指纹模板
    std::unique_ptr<ResetTask> CreateDeleteFingerprintsResetTask();

    // 重置业务数据表(不含casbin_rule/user_group/role), 清空会议/日志/热词等业务数据
    std::unique_ptr<ResetTask> CreateResetTablesResetTask();

    // 清理磁盘文件: 录音目录、临时目录、声纹目录
    std::unique_ptr<ResetTask> CreateCleanupFilesResetTask();

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_SYSTEM_RESET_TASK_H
