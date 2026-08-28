/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_DAO_MODELS_BMS_AUDIT_H
#define QIFENG_CA_INCLUDE_DAO_MODELS_BMS_AUDIT_H

#include <cstdint>
#include <string>

namespace qifeng_ca {
    namespace models {

        struct AuditRecord {
            uint64_t mId = 0;
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
            int64_t mCreateTime = 0;
        };

    }  // namespace models
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MODELS_BMS_AUDIT_H
