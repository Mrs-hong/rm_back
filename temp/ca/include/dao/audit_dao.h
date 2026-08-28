/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_DAO_AUDIT_DAO_H
#define QIFENG_CA_INCLUDE_DAO_AUDIT_DAO_H

#include <cstdint>
#include <string>
#include <vector>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_audit.h"

namespace qifeng_ca {

    // 过滤条件
    struct AuditSearchFilter {
        uint64_t mUserId = 0;
        std::vector<std::string> mUserAccounts;
        std::vector<std::string> mActions;
        int32_t mSuccessFilter = -1;
        int64_t mStartTime = 0;
        int64_t mEndTime = 0;
        int32_t mCurrent = 1;
        int32_t mPageSize = 20;
    };

    struct AuditSearchResult {
        int32_t mTotal = 0;
        std::vector<models::AuditRecord> mRecords;
    };

    class AuditDao : public BmsBaseDao {
    public:
        AuditDao() = default;
        ~AuditDao() override = default;

        AuditDao(const AuditDao &) = delete;
        AuditDao &operator=(const AuditDao &) = delete;
        AuditDao(AuditDao &&) = delete;
        AuditDao &operator=(AuditDao &&) = delete;

        bool Insert(const models::AuditRecord &record);

        models::AuditRecord GetById(uint64_t id);

        AuditSearchResult Search(const AuditSearchFilter &filter);

        bool DeleteByTime(int64_t beforeTime);

        int64_t CountByUserId(uint64_t userId);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_AUDIT_DAO_H
