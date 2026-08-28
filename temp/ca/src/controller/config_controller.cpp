//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "controller/config_controller.h"

namespace qifeng_ca {

    Status ConfigController::GetWebBaseConfig(const Empty &req, GetBaseConfigResponse &resp) {
        (void)req;
        SLOG_DEBUG << "GetWebBaseConfig";

        Status status = mConfigService.GetWebBaseConfig(&resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status ConfigController::SetWebBaseConfig(const SetBaseConfigRequest &req, Empty &resp) {
        (void)resp;
        SLOG_DEBUG << "SetWebBaseConfig - logCleanDays: " << req.log_clean_days();

        Status status = mConfigService.SetWebBaseConfig(req);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status ConfigController::GetSysBaseConfig(const Empty &req, GetSysBaseConfigResponse &resp) {
        (void)req;
        SLOG_DEBUG << "GetSysBaseConfig";

        Status status = mConfigService.GetSysBaseConfig(&resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status ConfigController::SetSysBaseConfig(const SetSysBaseConfigRequest &req, Empty &resp) {
        (void)resp;
        SLOG_DEBUG << "SetSysBaseConfig - diskWarning: " << req.disk_warning() << ", diskLimit: " << req.disk_limit();

        Status status = mConfigService.SetSysBaseConfig(req);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

}  // namespace qifeng_ca

QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::ConfigController);
