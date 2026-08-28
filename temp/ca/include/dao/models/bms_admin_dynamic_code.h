//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_MODELS_BMS_ADMIN_DYNAMIC_CODE_H
#define QIFENG_CA_INCLUDE_DAO_MODELS_BMS_ADMIN_DYNAMIC_CODE_H

#include <cstdint>
#include <string>

namespace qifeng_ca {
    namespace models {

        struct AdminDynamicCode {
            uint64_t mId = 0;
            std::string mDynamic;
            std::string mEncryptionCode;
            int64_t mExpireTime = 0;
            bool mIsUsed = false;
            int64_t mCreateTime = 0;
        };

    }  // namespace models
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MODELS_BMS_ADMIN_DYNAMIC_CODE_H
