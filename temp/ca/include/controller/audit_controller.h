//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_AUDIT_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_AUDIT_CONTROLLER_H

#include "common/audit_action_registry.h"
#include "drogon/DrObject.h"
#include "qifeng_ca/audit.pb.h"

#include "common/http/http.h"
#include "common/status.h"
#include "core/audit/audit_service.h"

namespace qifeng_ca {

    class AuditController final : public drogon::DrObject<AuditController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(AuditController);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("查询审计日志", false), SearchAuditLog,
                                    BmsPreAccountIdReq<AuditSearchRequest>, "/web/audit/search", drogon::Post);

        QIFENG_CA_METHOD_LIST_END;

        Status SearchAuditLog(const AuditSearchRequest &req, AuditSearchResponse &resp);

    private:
        AuditService mAuditService;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_AUDIT_CONTROLLER_H
