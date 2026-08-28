//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "controller/audit_controller.h"

namespace qifeng_ca {

    Status AuditController::SearchAuditLog(const AuditSearchRequest &req, AuditSearchResponse &resp) {
        Status status = mAuditService.SearchAuditLog(req, &resp);
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

}  // namespace qifeng_ca
QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::AuditController);
