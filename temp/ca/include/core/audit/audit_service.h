//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_AUDIT_AUDIT_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_AUDIT_AUDIT_SERVICE_H

#include "qifeng_ca/audit.pb.h"

#include "common/status.h"
#include "dao/audit_dao.h"

namespace qifeng_ca {

    class AuditService {
    public:
        AuditService() = default;
        ~AuditService() = default;

        AuditService(const AuditService &) = delete;
        AuditService &operator=(const AuditService &) = delete;
        AuditService(AuditService &&) noexcept = delete;
        AuditService &operator=(AuditService &&) noexcept = delete;

        Status SearchAuditLog(const AuditSearchRequest &req, AuditSearchResponse* resp);

    private:
        static AuditDao &GetDao();
        void FillSearchResponse(const AuditSearchResult &result, AuditSearchResponse* resp);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_AUDIT_AUDIT_SERVICE_H
