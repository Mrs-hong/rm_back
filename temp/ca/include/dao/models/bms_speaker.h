//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_MODELS_BMS_SPEAKER_H
#define QIFENG_CA_INCLUDE_DAO_MODELS_BMS_SPEAKER_H

#include <cstdint>
#include <string>
#include <vector>

namespace qifeng_ca {
    namespace models {

        struct Speaker {
            uint64_t mId = 0;
            uint64_t mAccountId = 0;
            std::string mNumber;
            std::string mFileName;
            std::string mSpeakerName;
            std::string mFeatures;
            int32_t mDim = 0;
            int64_t mRecordingTime = 0;
            int32_t mTotalTime = 0;
            int32_t mStatus = 0;
            std::string mRemark;
            int64_t mLastModifyTime = 0;
            std::string mSpeakerId;  // MD5
        };

    }  // namespace models
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MODELS_BMS_SPEAKER_H
