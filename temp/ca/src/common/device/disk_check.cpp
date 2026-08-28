/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "qifeng_framework/common/logger.h"

#include "common/config/device_config.h"
#include "common/device/disk_check.h"
#include "common/utils/disk_utils.h"
#include "common/ws/system_message_notifier.h"
#include "core/config/config_service.h"

namespace {
    constexpr int64_t CacheTtlMs = 5000;                        // 磁盘状态缓存 5 秒
    constexpr int DiskLowPercent = 1;                           // 磁盘可用低于 1% 视为不足
    constexpr int64_t DiskLowBytes = 1LL * 1024 * 1024 * 1024;  // 磁盘可用低于 1GB 视为不足

    // 扣除预测大小, 返回失败 Status 表示空间不够
    qifeng_ca::Status ApplyPrediction(const qifeng_ca::DiskSpaceInfo &info, int64_t predictedBytes, int &usagePercent,
                                      int64_t &availBytes) {
        availBytes = static_cast<int64_t>(info.mAvailBytes);
        if (availBytes < predictedBytes) {
            SLOG_WARN << "DiskCheck: predicted " << (predictedBytes / (1024LL * 1024)) << "MB exceeds avail "
                      << (info.mAvailBytes / (1024ULL * 1024)) << "MB";
            return qifeng_ca::Status {-1, "系统磁盘空间不足"};
        }
        availBytes -= predictedBytes;
        if (info.mTotalBytes > 0) {
            usagePercent =
                static_cast<int>((static_cast<int64_t>(info.mTotalBytes - info.mAvailBytes) + predictedBytes) * 100LL /
                                 static_cast<int64_t>(info.mTotalBytes));
        }
        return qifeng_ca::Status {};
    }

    // 检查硬限制和软限制阈值, 返回失败 Status 表示磁盘不足
    qifeng_ca::Status CheckThreshold(uint64_t accountId, int usagePercent, int64_t availBytes, bool flag) {
        if (usagePercent >= (100 - DiskLowPercent) || availBytes < DiskLowBytes) {
            SLOG_WARN << "DiskCheck: disk low, usage=" << usagePercent << "%, avail=" << (availBytes / (1024LL * 1024))
                      << "MB";
            return qifeng_ca::Status {-1, "磁盘空间已达到设置的上限"};
        }
        qifeng_ca::ConfigService configSvc;
        qifeng_ca::GetSysBaseConfigResponse cfg;
        configSvc.GetSysBaseConfig(&cfg);
        if (cfg.disk_limit() > 0 && usagePercent >= cfg.disk_limit()) {
            SLOG_WARN << "DiskCheck: usage " << usagePercent << "% exceeds limit " << cfg.disk_limit() << "%";
            return qifeng_ca::Status {-1, "磁盘空间已达到设置的上限"};
        }
        if (flag && cfg.disk_warning() > 0 && usagePercent >= cfg.disk_warning()) {
            SLOG_WARN << "DiskCheck: usage " << usagePercent << "% exceeds warning " << cfg.disk_warning() << "%";
            qifeng_ca::SystemMessageNotifier::GetInstance().SendDiskWarning(accountId);
        }
        SLOG_DEBUG << "DiskCheck: usage " << usagePercent << "% exceeds warning " << cfg.disk_warning() << "%"
                   << " flag=" << flag;
        return qifeng_ca::Status {};
    }
}  // namespace

namespace qifeng_ca {

    Status DiskCheck::IsDiskLow(uint64_t accountId, int64_t predictedBytes, bool flag) {
        if (predictedBytes == 0) {
            std::lock_guard<std::mutex> lock(mMutex);
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - mCache.mCacheTime).count();
            if (elapsed < CacheTtlMs) {
                return mCache.mDiskStatus;
            }
        }

        auto spaceInfo = DiskUtils::GetDiskSpace(DeviceConfig::GetInstance().GetDataDiskMountPath());
        if (!spaceInfo.mValid) {
            return Status {};
        }

        int usagePercent = spaceInfo.mUsagePercent;
        int64_t availBytes = static_cast<int64_t>(spaceInfo.mAvailBytes);

        if (predictedBytes > 0) {
            Status predStatus = ApplyPrediction(spaceInfo, predictedBytes, usagePercent, availBytes);
            if (!predStatus.IsSuccess()) {
                return predStatus;
            }
        }

        Status diskStatus = CheckThreshold(accountId, usagePercent, availBytes, flag);

        if (predictedBytes == 0) {
            std::lock_guard<std::mutex> lock(mMutex);
            mCache.mDiskStatus = diskStatus;
            mCache.mCacheTime = std::chrono::steady_clock::now();
        }

        return diskStatus;
    }

}  // namespace qifeng_ca