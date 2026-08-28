//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_FINGERPRINT_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_FINGERPRINT_CONFIG_H

#include <cstdint>
#include <ctime>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class FingerprintConfig {
    public:
        static FingerprintConfig &GetInstance() {
            static FingerprintConfig Instance;
            return Instance;
        }

        // 单次指纹操作超时(毫秒)
        uint32_t GetEnrollTimeoutMs() const {
            int val = CONFIG_MANAGER.GetInt("fingerprint_enroll", "enroll_timeout_ms", 10000);
            return (val < 1000 || val > 60000) ? 10000 : static_cast<uint32_t>(val);
        }

        // 取消操作超时(毫秒, 快速退出)
        uint32_t GetCancelTimeoutMs() const {
            int val = CONFIG_MANAGER.GetInt("fingerprint_enroll", "cancel_timeout_ms", 1000);
            return (val < 100 || val > 10000) ? 1000 : static_cast<uint32_t>(val);
        }

        // 需要成功采集次数
        uint8_t GetRequiredSuccesses() const {
            int val = CONFIG_MANAGER.GetInt("fingerprint_enroll", "required_successes", 6);
            return (val < 1 || val > 10) ? 6 : static_cast<uint8_t>(val);
        }

        // 最多调用次数
        uint8_t GetMaxTotalCalls() const {
            int val = CONFIG_MANAGER.GetInt("fingerprint_enroll", "max_total_calls", 9);
            return (val < 1 || val > 20) ? 9 : static_cast<uint8_t>(val);
        }

        // 最多失败次数(超过则取消)
        uint8_t GetMaxFailures() const {
            int val = CONFIG_MANAGER.GetInt("fingerprint_enroll", "max_failures", 4);
            return (val < 1 || val > 10) ? 4 : static_cast<uint8_t>(val);
        }

        // 录入总超时(秒)
        time_t GetEnrollSessionTimeoutSec() const {
            int val = CONFIG_MANAGER.GetInt("fingerprint_enroll", "session_timeout_sec", 120);
            return (val < 30 || val > 600) ? 120 : static_cast<time_t>(val);
        }

    private:
        FingerprintConfig() = default;
        ~FingerprintConfig() = default;
        FingerprintConfig(const FingerprintConfig &) = delete;
        FingerprintConfig &operator=(const FingerprintConfig &) = delete;
        FingerprintConfig(FingerprintConfig &&) = delete;
        FingerprintConfig &operator=(FingerprintConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_FINGERPRINT_CONFIG_H
