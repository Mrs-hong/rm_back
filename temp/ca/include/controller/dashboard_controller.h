//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_DASHBOARD_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_DASHBOARD_CONTROLLER_H

#include "drogon/DrObject.h"
#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/device.pb.h"

#include "common/http/http.h"
#include "common/status.h"

namespace qifeng_ca {

    // 仪表盘控制器: 提供磁盘可用时长与纪要统计信息查询, 所有登录用户均可访问
    class DashboardController final : public drogon::DrObject<DashboardController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(DashboardController);

        // 获取磁盘可用时长信息
        QIFENG_CA_METHOD_ADD(ActionDesc("获取磁盘可用时长", false), GetDiskAvailableInfo, "/web/dashboard/getDiskAvailableInfo",
                             drogon::Post);

        // 获取纪要统计信息
        QIFENG_CA_METHOD_ADD(ActionDesc("获取纪要统计", false), GetSummaryStats, "/web/dashboard/getSummaryStats", drogon::Post);

        QIFENG_CA_METHOD_LIST_END;

        Status GetDiskAvailableInfo(const Empty &req, DiskAvailableInfoResponse &resp);

        Status GetSummaryStats(const Empty &req, SummaryStatsResponse &resp);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_DASHBOARD_CONTROLLER_H
