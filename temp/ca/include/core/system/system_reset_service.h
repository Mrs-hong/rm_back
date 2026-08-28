//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_SYSTEM_SYSTEM_RESET_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_SYSTEM_SYSTEM_RESET_SERVICE_H

#include <cstdint>
#include <memory>
#include <vector>

#include "common/status.h"
#include "core/system/reset_task.h"

namespace qifeng_ca {

    // 系统重置服务: 管理重置任务注册与按序执行
    // 重置流程: 1.删除所有用户 2.删除设备指纹 3.重置数据表并恢复默认
    class SystemResetService {
    public:
        static SystemResetService &GetInstance();

        ~SystemResetService() = default;
        SystemResetService(const SystemResetService &) = delete;
        SystemResetService &operator=(const SystemResetService &) = delete;
        SystemResetService(SystemResetService &&) = delete;
        SystemResetService &operator=(SystemResetService &&) = delete;

        // 注册一个重置任务(追加到任务列表末尾)
        void RegisterTask(std::unique_ptr<ResetTask> task);

        // 执行系统重置: 按注册顺序依次执行所有重置任务
        // operatorAccountId 必须为管理员账户
        Status ResetSystem(uint64_t operatorAccountId);

    private:
        SystemResetService();
        void RegisterDefaultTasks();

        std::vector<std::unique_ptr<ResetTask>> mTasks;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_SYSTEM_SYSTEM_RESET_SERVICE_H
