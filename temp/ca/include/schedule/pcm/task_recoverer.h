//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_SCHEDULE_PCM_TASK_RECOVERER_H
#define QIFENG_CA_INCLUDE_SCHEDULE_PCM_TASK_RECOVERER_H

#include <vector>

#include "schedule/task/base_task.h"

namespace qifeng_ca {

    // 任务恢复器: 进程启动时从DB的Audio表恢复未完成任务
    //   仅负责扫描Audio表中处于中间状态(录音中/转写中/总结中)的记录,
    class TaskRecoverer {
    public:
        TaskRecoverer() = default;

        // 从DB恢复未完成任务, 返回任务列表(由PcmEngine提交到TaskScheduler)
        // 恢复顺序: 中断录音 -> 中断转写 -> 中断纪要
        std::vector<TaskPtr> RecoverFromMeetingDb();
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_SCHEDULE_PCM_TASK_RECOVERER_H
