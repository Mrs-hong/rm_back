//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "controller/upgrade_controller.h"
#include "core/upgrade/upgrade_realtime.h"

namespace qifeng_ca {

    Status UpgradeController::Upgrade(const UpgradeRequest &req, UpgradeResponse &resp) {
        SLOG_INFO << "Upgrade: account=" << req.account_id() << ", package=" << req.package_path();

        // 两阶段升级: PrepareUpgrade(验证+部署+识别) 同步返回, ExecuteUpgrade 异步执行
        Status status = mUpgradeService.PrepareUpgrade(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status UpgradeController::GetUpgradeResult(const Empty &req, GetUpgradeResultResponse &resp) {
        (void)req;
        SLOG_DEBUG << "GetUpgradeResult";

        Status status = mUpgradeService.GetUpgradeResult(&resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status UpgradeController::ApplyOta(const UpgradeRequest &req, UpgradeResponse &resp) {
        SLOG_INFO << "ApplyOta: account=" << req.account_id();

        // 复用已下载待确认的 OTA 升级包, 由 UpgradeRealtimeManager 移交 UpgradeService 执行升级
        Status status = UpgradeRealtimeManager::GetInstance().ApplyPendingOta(req.account_id(), &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status UpgradeController::CancelOta(const Empty &req, UpgradeResponse &resp) {
        (void)req;
        SLOG_INFO << "CancelOta";

        Status status = UpgradeRealtimeManager::GetInstance().CancelPendingOta();
        if (status.GetCode() != 0) {
            resp.set_verified(false);
            resp.set_message(status.GetMsg());
            FLOG_ERROR(status.ToString());
        } else {
            resp.set_verified(true);
            resp.set_message("OTA升级已取消");
        }
        return status;
    }

}  // namespace qifeng_ca

QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::UpgradeController);
