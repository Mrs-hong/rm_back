//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "dao/cleanup_dao.h"

namespace qifeng_ca {

    CleanupDbResult CleanupDao::DeleteExpiredRecords(int64_t auditBeforeTime, int64_t deviceBeforeTime) {
        CleanupDbResult result {};
        try {
            soci::session session = GetSession();
            session.begin();

            try {
                soci::statement auditStmt =
                    (session.prepare << "DELETE FROM audit_log WHERE create_time < :before_time",
                     soci::use(auditBeforeTime, "before_time"));
                auditStmt.execute(true);
                result.mDeletedAuditCount = static_cast<int64_t>(auditStmt.get_affected_rows());

                soci::statement deviceStmt =
                    (session.prepare << "DELETE FROM device WHERE timestamp < :before_time",
                     soci::use(deviceBeforeTime, "before_time"));
                deviceStmt.execute(true);
                result.mDeletedDeviceCount = static_cast<int64_t>(deviceStmt.get_affected_rows());

                session.commit();
                result.mSuccess = true;

                SLOG_INFO << "CleanupDao: deleted audit=" << result.mDeletedAuditCount
                          << ", device=" << result.mDeletedDeviceCount;
            } catch (const std::exception &e) {
                session.rollback();
                SLOG_ERROR << "CleanupDao::DeleteExpiredRecords transaction failed: " << e.what();
                result.mSuccess = false;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "CleanupDao::DeleteExpiredRecords failed to get session: " << e.what();
            result.mSuccess = false;
        }
        return result;
    }

}  // namespace qifeng_ca
