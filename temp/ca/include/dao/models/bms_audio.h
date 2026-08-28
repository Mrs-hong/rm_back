//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_MODELS_BMS_AUDIO_H
#define QIFENG_CA_INCLUDE_DAO_MODELS_BMS_AUDIO_H

#include <cstdint>
#include <string>

namespace qifeng_ca {
    namespace models {

        struct Audio {
            uint64_t mId = 0;
            std::string mAudioId;
            uint64_t mAccountId = 0;
            int mSource = 0;
            int mIsRecording = 0;
            std::string mTheme;
            std::string mModerator;
            std::string mAttendees;
            std::string mPlaces;
            std::string mFileName;
            int64_t mRecordingTime = 0;
            int mTotalTime = 0;
            int mStatus = 0;
            int64_t mPlanFinishTime = 0;
            std::string mRemark;
            int64_t mTimestamp = 0;
            int mKind = 1;
            std::string mKeywords;
            int64_t mUpdateTime = 0;
            std::string mMarkers;
            std::string mSumModelName;
            uint64_t mNoteId = 0;
            int64_t mTransDuration = 0;
            int64_t mSumDuration = 0;
            int mTransRetryCount = 0;
            int mTransLastProgress = 0;
            int mSumLastProgress = 0;
            int mTransLastUpdateTime = 0;
            int64_t mTransStartTime = 0;
            int64_t mSumStartTime = 0;
            int mTransErrorCode = 0;
            std::string mMessage;
            int mUseNote = 0;
            int mReSummury = 0;
            int mSumWordCount = 0;
        };

    }  // namespace models
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MODELS_BMS_AUDIO_H
