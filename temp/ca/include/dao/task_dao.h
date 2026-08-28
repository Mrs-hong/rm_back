//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_TASK_DAO_H
#define QIFENG_CA_INCLUDE_DAO_TASK_DAO_H

#include <string>
#include <vector>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_task.h"

namespace qifeng_ca {

    class TaskDao : public BmsBaseDao {
    public:
        TaskDao() = default;
        ~TaskDao() override = default;

        TaskDao(const TaskDao &) = delete;
        TaskDao &operator=(const TaskDao &) = delete;
        TaskDao(TaskDao &&) = delete;
        TaskDao &operator=(TaskDao &&) = delete;

        // 插入任务记录
        bool Insert(const models::BmsTask &task);

        // 根据taskId查询
        models::BmsTask GetByTaskId(const std::string &taskId);

        // 根据audioId查询
        std::vector<models::BmsTask> GetByAudioId(const std::string &audioId);

        // 查询所有未完成任务(status != 3完成 且 status != 8永久失败)
        std::vector<models::BmsTask> GetPendingTasks();

        // 更新任务状态和阶段
        bool UpdateStatus(const std::string &taskId, int taskStatus, int taskPhase);

        // 更新任务全部字段(含taskData JSON)
        bool UpdateTaskData(const models::BmsTask &task);

        // 更新任务错误信息
        bool UpdateError(const std::string &taskId, int taskStatus, const std::string &errorMessage);

        // 增加重试计数
        bool IncrementRetryCount(const std::string &taskId);

        // 删除任务
        bool DeleteByTaskId(const std::string &taskId);

        // 删除已完成任务(清理)
        // TODO(yf): 定期清理功能
        bool DeleteCompletedTasks();
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_TASK_DAO_H
