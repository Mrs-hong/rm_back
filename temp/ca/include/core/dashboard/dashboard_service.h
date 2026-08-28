//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_DASHBOARD_DASHBOARD_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_DASHBOARD_DASHBOARD_SERVICE_H

#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/device.pb.h"

#include "common/status.h"

namespace qifeng_ca {

    // 磁盘可用时长信息(页面与显示屏共用, 保证一致)
    struct DiskTimeInfo {
        int32_t availableHours = 0;  // 可用时长(小时)
        int32_t totalHours = 0;      // 总可用时长(小时)
        int32_t ratioPercent = 0;    // 可用比例百分比(0~100)
    };

    // 仪表盘服务: 提供磁盘可用时长与纪要统计信息查询
    class DashboardService {
    public:
        DashboardService() = default;
        ~DashboardService() = default;

        DashboardService(const DashboardService &) = delete;
        DashboardService &operator=(const DashboardService &) = delete;
        DashboardService(DashboardService &&) noexcept = delete;
        DashboardService &operator=(DashboardService &&) = delete;

        // 获取磁盘可用时长信息
        Status GetDiskAvailableInfo(DiskAvailableInfoResponse* resp);

        // 获取纪要统计信息(基于audios表)
        Status GetSummaryStats(SummaryStatsResponse* resp);

        // 采集磁盘可用时长信息(页面与显示屏复用, 保证一致)
        // 按3000小时上限, 根据audios表总时长计算剩余可用时长
        static DiskTimeInfo CollectDiskTimeInfo();
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_DASHBOARD_DASHBOARD_SERVICE_H
