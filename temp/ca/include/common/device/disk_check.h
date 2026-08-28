//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_DEVICE_DISK_CHECK_H
#define QIFENG_CA_INCLUDE_COMMON_DEVICE_DISK_CHECK_H

#include <chrono>
#include <cstdint>
#include <mutex>

#include "common/status.h"

namespace qifeng_ca {
    class DiskCheck {
    public:
        static DiskCheck &GetInstance() {
            static DiskCheck Instance;
            return Instance;
        }

        // predictedBytes == 0: 使用缓存的磁盘状态
        // predictedBytes != 0: 不走缓存, 实时检测剩余空间是否足够容纳
        // flag == true: 达到 disk_warning 阈值时发送告警通知, 但不返回失败(默认开启告警)
        // 返回 Status: 成功表示磁盘可用, 失败表示磁盘不足(含具体原因)
        Status IsDiskLow(uint64_t accountId, int64_t predictedBytes = 0, bool flag = true);

    private:
        DiskCheck() = default;

        struct DiskCache {
            Status mDiskStatus;
            std::chrono::steady_clock::time_point mCacheTime;
        };

        std::mutex mMutex;
        DiskCache mCache;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_DEVICE_DISK_CHECK_H
