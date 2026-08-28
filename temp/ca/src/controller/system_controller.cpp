//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "controller/system_controller.h"

namespace qifeng_ca {

    Status SystemController::ResetSystem(const AccountRequst &req, Empty &resp) {
        (void)resp;
        SLOG_INFO << "ResetSystem: operator=" << req.account_id();
        Status status = mResetService.ResetSystem(req.account_id());
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status SystemController::DiagnoseLog(const AccountRequst &req, DiagnoseLogResponse &resp) {
        SLOG_INFO << "DiagnoseLog: operator=" << req.account_id();
        Status status = mDiagnoseService.GenerateDiagnoseLog(&resp);
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
            return status;
        }
        return Status {};
    }

}  // namespace qifeng_ca

QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::SystemController);
