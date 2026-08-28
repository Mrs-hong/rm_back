//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_MODELS_BMS_TRANS_H
#define QIFENG_CA_INCLUDE_DAO_MODELS_BMS_TRANS_H

#include <cstdint>
#include <string>

namespace qifeng_ca {
    namespace models {

        struct Trans {
            uint64_t mId = 0;
            uint64_t mAccountId = 0;
            std::string mAudioId;
            int32_t mStartTime = 0;
            int32_t mEndTime = 0;
            int32_t mSpeaker = 0;
            std::string mSpeakerName;
            std::string mContent;
            int mIsOrig = 1;
            int mIsDiscard = 0;
            int mSegFlag = 0;
        };

    }  // namespace models
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MODELS_BMS_TRANS_H
