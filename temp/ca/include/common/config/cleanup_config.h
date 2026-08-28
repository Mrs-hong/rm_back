//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_CLEANUP_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_CLEANUP_CONFIG_H

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    // 数据清理任务配置项
    class CleanupConfig {
    public:
        static CleanupConfig &GetInstance() {
            static CleanupConfig Instance;
            return Instance;
        }

        // 定时清理周期(秒), 默认1小时
        int GetIntervalSec() const {
            int interval = CONFIG_MANAGER.GetInt("cleanup", "interval_sec", 3600);
            return (interval < 60) ? 3600 : interval;
        }

        // 单次清理任务超时时间(秒), 默认5分钟
        int GetTimeoutSec() const {
            int timeout = CONFIG_MANAGER.GetInt("cleanup", "timeout_sec", 300);
            return (timeout < 10) ? 300 : timeout;
        }

        // 临时文件最大保留时长(秒), 默认1小时
        int GetTempFileMaxAgeSec() const {
            int age = CONFIG_MANAGER.GetInt("cleanup", "temp_file_max_age_sec", 3600);
            return (age < 0) ? 3600 : age;
        }

        // 磁盘空间检查缓存TTL(秒), 默认5秒
        int GetDiskCheckCacheTtlSec() const {
            int ttl = CONFIG_MANAGER.GetInt("cleanup", "disk_check_cache_ttl_sec", 5);
            return (ttl < 1) ? 5 : ttl;
        }

        // 清理失败重试次数, 默认3次
        int GetRetryCount() const {
            int count = CONFIG_MANAGER.GetInt("cleanup", "retry_count", 3);
            return (count < 0) ? 3 : count;
        }

        // 重试间隔(秒), 默认5秒
        int GetRetryIntervalSec() const {
            int interval = CONFIG_MANAGER.GetInt("cleanup", "retry_interval_sec", 5);
            return (interval < 1) ? 5 : interval;
        }

    private:
        CleanupConfig() = default;
        ~CleanupConfig() = default;
        CleanupConfig(const CleanupConfig &) = delete;
        CleanupConfig &operator=(const CleanupConfig &) = delete;
        CleanupConfig(CleanupConfig &&) = delete;
        CleanupConfig &operator=(CleanupConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_CLEANUP_CONFIG_H
