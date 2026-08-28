//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_MODELS_BMS_SUMMARY_H
#define QIFENG_CA_INCLUDE_DAO_MODELS_BMS_SUMMARY_H

#include <cstdint>
#include <string>

namespace qifeng_ca {
    namespace models {

        struct Summary {
            uint64_t mId = 0;
            uint64_t mAccountId = 0;
            std::string mAudioId;
            std::string mContent;
            int mIsOrig = 1;
            int mIsDiscard = 0;
            uint64_t mTimestamp = 0;
        };

    }  // namespace models
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MODELS_BMS_SUMMARY_H
