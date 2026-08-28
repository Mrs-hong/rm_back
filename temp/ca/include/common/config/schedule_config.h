//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_SCHEDULE_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_SCHEDULE_CONFIG_H

#include <cstdint>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class ScheduleConfig {
    public:
        static ScheduleConfig &GetInstance() {
            static ScheduleConfig Instance;
            return Instance;
        }

        // 总结任务超时(秒), 默认60分钟
        int GetSummaryTimeoutSec() const {
            int val = CONFIG_MANAGER.GetInt("schedule", "summary_timeout_sec", 3600);
            return (val < 60 || val > 1440) ? 1440 : val;
        }

        // 离线转写超时(毫秒), 默认120分钟
        uint64_t GetOfflineTransTimeoutMs() const {
            int val = CONFIG_MANAGER.GetInt("schedule", "offline_trans_timeout_sec", 3600);
            return (val < 60 || val > 10800) ? 10800LL * 1000ULL : static_cast<uint64_t>(val) * 1000ULL;
        }

    private:
        ScheduleConfig() = default;
        ~ScheduleConfig() = default;
        ScheduleConfig(const ScheduleConfig &) = delete;
        ScheduleConfig &operator=(const ScheduleConfig &) = delete;
        ScheduleConfig(ScheduleConfig &&) = delete;
        ScheduleConfig &operator=(ScheduleConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_SCHEDULE_CONFIG_H
