//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_UTILS_DISK_UTILS_H
#define QIFENG_CA_INCLUDE_COMMON_UTILS_DISK_UTILS_H

#include <cstdint>
#include <string>

namespace qifeng_ca {
    // 磁盘空间信息
    struct DiskSpaceInfo {
        uint64_t mTotalBytes {0};
        uint64_t mUsedBytes {0};
        uint64_t mAvailBytes {0};
        int mUsagePercent {0};  // 0~100
        bool mValid {false};
    };

    // 磁盘IO统计信息
    struct DiskIOStat {
        float mQueue {0.0F};      // 当前IO队列深度
        float mIOPS {0.0F};       // 每秒IO操作数
        float mIOLatency {0.0F};  // 平均IO延迟(ms)
        bool mValid {false};
    };

    class DiskUtils {
    public:
        // 获取指定路径的磁盘空间信息(总空间/已用空间/可用空间/使用率)
        static DiskSpaceInfo GetDiskSpace(const std::string &path);

        // 获取磁盘IO统计(IOPS/队列深度/IO延迟), 通过读取/proc/diskstats实现
        static DiskIOStat GetDiskIOStat(const std::string &path);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_UTILS_DISK_UTILS_H
