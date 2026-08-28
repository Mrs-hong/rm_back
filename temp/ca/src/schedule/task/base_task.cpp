//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "schedule/task/base_task.h"

#include "common/common.h"

namespace qifeng_ca {

    BaseTask::BaseTask(std::string audioId, uint64_t accountId) : mAudioId(std::move(audioId)), mAccountId(accountId) {
        mTaskId = GenUUID();
    }

    BaseTask::~BaseTask() = default;

    void BaseTask::SetComplete() {
        mRunning.store(false, std::memory_order_release);
        mComplete.store(true, std::memory_order_release);
    }

    void BaseTask::SetRunning(bool running) {
        mRunning.store(running, std::memory_order_release);
    }

}  // namespace qifeng_ca
