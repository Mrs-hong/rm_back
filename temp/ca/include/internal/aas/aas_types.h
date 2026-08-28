//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_AAS_AAS_TYPES_H
#define QIFENG_CA_INCLUDE_INTERNAL_AAS_AAS_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace qifeng_ca {

    struct AasSegment {
        int32_t mOrder {0};
        int32_t mStartMs {0};
        int32_t mEndMs {0};
        std::string mText;
        std::string mSpeakerId;
        int32_t mSpeakerLabel;
        std::string mSpeakerName;
        bool mIsFullSegment {false};  // 是否完整片段
    };

    struct AasResult {
        int32_t mCode {0};
        std::string mMessage;
        std::string mTraceId;
        std::string mText;
        int32_t mSpeakerLabel;
        std::vector<float> mSvEmbedding;
        std::vector<AasSegment> mSegments;
        int64_t mProcessingTimeMs {0};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_AAS_AAS_TYPES_H
