/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_DAO_MANAGERS_AUDIT_DAO_MANAGER_H
#define QIFENG_CA_INCLUDE_DAO_MANAGERS_AUDIT_DAO_MANAGER_H

#include <cstdint>
#include <memory>
#include <string>

#include "dao/audit_dao.h"
#include "dao/models/bms_audit.h"

namespace qifeng_ca {

    struct AuditRecordParam {
        uint64_t mUserId = 0;
        std::string mUserAccount;
        std::string mUserName;
        uint64_t mUserGroupId = 0;
        std::string mAction;
        std::string mActionDesc;
        std::string mApiPath;
        std::string mHttpMethod;
        bool mSuccess = true;
        std::string mErrorMessage;
        std::string mClientIp;
        std::string mRequestData;
    };

    class AuditDaoManager {
    public:
        static AuditDaoManager &GetInstance();

        ~AuditDaoManager() = default;

        AuditDaoManager(const AuditDaoManager &) = delete;
        AuditDaoManager &operator=(const AuditDaoManager &) = delete;
        AuditDaoManager(AuditDaoManager &&) = delete;
        AuditDaoManager &operator=(AuditDaoManager &&) = delete;

        bool Record(const AuditRecordParam &param);

        models::AuditRecord GetById(uint64_t id);

        AuditSearchResult Search(const AuditSearchFilter &filter);

        bool CleanExpired(int64_t beforeTime);

        int64_t CountByUserId(uint64_t userId);

    private:
        AuditDaoManager();

        std::shared_ptr<AuditDao> mDao;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MANAGERS_AUDIT_DAO_MANAGER_H
