//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_DISK_MONITOR_H
#define QIFENG_CA_INCLUDE_INTERNAL_DISK_MONITOR_H

#include <cstdint>
#include <string>

#include "common/utils/disk_utils.h"

namespace qifeng_ca {

    // 磁盘告警级别
    enum class DiskAlertLevel : uint8_t {
        Normal = 0,  // 正常
        SoftLimit,   // 软限制告警(慢闪红灯)
        HardLimit    // 硬限制告警(快闪红灯)
    };

    class DiskMonitor {
    public:
        static DiskMonitor &GetInstance();

        // 检查磁盘空间并更新LED状态
        // 返回当前告警级别
        DiskAlertLevel CheckAndUpdateLed();

        // 获取磁盘空间信息
        DiskSpaceInfo GetDiskSpace(const std::string &path = "/");

        // 检查磁盘是否有足够空间录音
        bool CanStartRecording(const std::string &path = "/");

        // 获取磁盘可用时长(分钟, 基于录音码率估算)
        int32_t GetAvailableMinutes(const std::string &path = "/");

        // 获取磁盘总可用时长(分钟, 基于磁盘总容量与录音码率估算)
        int32_t GetTotalMinutes(const std::string &path = "/");

    private:
        DiskMonitor() = default;

        // 计算告警级别
        DiskAlertLevel CalcAlertLevel(const DiskSpaceInfo &info);

        // 更新LED状态
        void UpdateLedAlert(DiskAlertLevel level);

        // 获取录音码率(字节/秒)
        int64_t GetRecordingBitrate() const;

        DiskAlertLevel mCurrentLevel {DiskAlertLevel::Normal};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_DISK_MONITOR_H
