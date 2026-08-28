/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_DAO_MODELS_BMS_HOTWORD_H
#define QIFENG_CA_INCLUDE_DAO_MODELS_BMS_HOTWORD_H

#include <cstdint>
#include <string>

namespace qifeng_ca {
    namespace models {

        struct HotWord {
            uint64_t mId = 0;
            uint64_t mAccountId = 0;
            std::string mWord;
            std::string mRemark;
            int32_t mStatus = 1;
            int64_t mCreateTime = 0;
            int64_t mUpdateTime = 0;
        };

    }  // namespace models
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MODELS_BMS_HOTWORD_H
