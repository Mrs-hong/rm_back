//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "controller/dashboard_controller.h"

#include "core/dashboard/dashboard_service.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng_ca {

    Status DashboardController::GetDiskAvailableInfo(const Empty &req, DiskAvailableInfoResponse &resp) {
        (void)req;
        DashboardService svc;
        Status status = svc.GetDiskAvailableInfo(&resp);
        if (status.GetCode() != 0) {
            SLOG_WARN << "DashboardController: getDiskAvailableInfo failed, msg=" << status.ToString();
        }
        return status;
    }

    Status DashboardController::GetSummaryStats(const Empty &req, SummaryStatsResponse &resp) {
        (void)req;
        DashboardService svc;
        Status status = svc.GetSummaryStats(&resp);
        if (status.GetCode() != 0) {
            SLOG_WARN << "DashboardController: getSummaryStats failed, msg=" << status.ToString();
        }
        return status;
    }

}  // namespace qifeng_ca

QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::DashboardController);
