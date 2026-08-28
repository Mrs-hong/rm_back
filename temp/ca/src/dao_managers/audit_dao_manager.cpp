/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "qifeng_framework/common/logger.h"

#include "common/common.h"
#include "dao_managers/audit_dao_manager.h"

namespace qifeng_ca {

    AuditDaoManager &AuditDaoManager::GetInstance() {
        static AuditDaoManager Instance;
        return Instance;
    }

    AuditDaoManager::AuditDaoManager() : mDao(std::make_shared<AuditDao>()) {
        SLOG_INFO << "AuditDaoManager initialized (no cache)";
    }

    bool AuditDaoManager::Record(const AuditRecordParam &param) {
        SLOG_DEBUG << "AuditDaoManager::Record - userId: " << param.mUserId << ", action: " << param.mAction
                   << ", apiPath: " << param.mApiPath << ", success: " << param.mSuccess;

        models::AuditRecord record;
        record.mUserId = param.mUserId;
        record.mUserAccount = param.mUserAccount;
        record.mUserName = param.mUserName;
        record.mUserGroupId = param.mUserGroupId;
        record.mAction = param.mAction;
        record.mActionDesc = param.mActionDesc;
        record.mApiPath = param.mApiPath;
        record.mHttpMethod = param.mHttpMethod;
        record.mSuccess = param.mSuccess;
        record.mErrorMessage = param.mErrorMessage;
        record.mClientIp = param.mClientIp;
        record.mRequestData = param.mRequestData;
        record.mCreateTime = static_cast<long long>(GetTimeMs());

        return mDao->Insert(record);
    }

    models::AuditRecord AuditDaoManager::GetById(uint64_t id) {
        return mDao->GetById(id);
    }

    AuditSearchResult AuditDaoManager::Search(const AuditSearchFilter &filter) {
        return mDao->Search(filter);
    }

    bool AuditDaoManager::CleanExpired(int64_t beforeTime) {
        return mDao->DeleteByTime(beforeTime);
    }

    int64_t AuditDaoManager::CountByUserId(uint64_t userId) {
        return mDao->CountByUserId(userId);
    }

}  // namespace qifeng_ca
