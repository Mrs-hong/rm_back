//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_MODELS_BMS_TASK_H
#define QIFENG_CA_INCLUDE_DAO_MODELS_BMS_TASK_H

#include <cstdint>
#include <string>

namespace qifeng_ca {
    namespace models {

        struct BmsTask {
            uint64_t mId = 0;
            std::string mTaskId;
            std::string mAudioId;
            uint64_t mAccountId = 0;
            int mTaskType = 0;
            int mTaskStatus = 0;
            int mTaskPhase = 0;
            int mRunMode = 0;
            uint64_t mTimeoutTs = 0;
            int mRetryCount = 0;
            std::string mErrorMessage;
            std::string mTaskData;  // PcmTaskInfo proto JSON
            int64_t mCreateTime = 0;
            int64_t mUpdateTime = 0;
        };

    }  // namespace models
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MODELS_BMS_TASK_H
