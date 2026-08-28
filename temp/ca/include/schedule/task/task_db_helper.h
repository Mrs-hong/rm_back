//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_SCHEDULE_TASK_TASK_DB_HELPER_H
#define QIFENG_CA_INCLUDE_SCHEDULE_TASK_TASK_DB_HELPER_H

#include <cstdint>
#include <string>
#include <string_view>

#include "common/audio_enums.h"

namespace qifeng_ca {

    // 任务数据库辅助: 仅提供音频状态更新的静态函数
    struct TaskDbHelper {
        // 更新音频状态(message 推荐使用 AudioStatusMsg 命名空间中的固定常量)
        static bool UpdateAudioStatus(uint64_t accountId, const std::string &audioId, AudioStatus status,
                                      std::string_view message);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_SCHEDULE_TASK_TASK_DB_HELPER_H
