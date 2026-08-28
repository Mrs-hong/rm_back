//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_MODELS_MODELS_H
#define QIFENG_CA_INCLUDE_DAO_MODELS_MODELS_H

#include <cstdint>
#include <string>

namespace qifeng_ca {
    namespace models {

        struct User {
            uint64_t mId = 0;
            uint64_t mAccountId = 0;
            std::string mAccount;
            std::string mPassword;
            std::string mUserName;
            std::string mEmail;
            std::string mPhone;
            uint64_t mFingerprintId = 0;
            uint64_t mGroupId = 1;
            std::string mComment;
            int64_t mCreateTime = 0;
            std::string mAccessToken;
            std::string mRefreshToken;
        };

        struct UserGroup {
            uint64_t mId = 0;
            std::string mName;
            std::string mDescription;
            bool mIsSystem = false;
            std::string mCreatedAt;
            std::string mUpdatedAt;
        };

    }  // namespace models
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MODELS_MODELS_H
