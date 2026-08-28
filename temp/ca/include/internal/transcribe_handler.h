//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_TRANSCRIBE_HANDLER_H
#define QIFENG_CA_INCLUDE_INTERNAL_TRANSCRIBE_HANDLER_H

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/aas/aas_types.h"
#include "qifeng_framework/aas/aas_callback.h"

namespace qifeng_ca {

    class TranscribeHandler {
    public:
        TranscribeHandler(const std::string &audioId, uint64_t accountId, bool isRealtime);

        ~TranscribeHandler();

        TranscribeHandler(const TranscribeHandler &) = delete;
        TranscribeHandler &operator=(const TranscribeHandler &) = delete;
        TranscribeHandler(TranscribeHandler &&) = delete;
        TranscribeHandler &operator=(TranscribeHandler &&) = delete;

        void OnAasResult(const qifeng::aas::AasResult &result);

        void NotifyFinal();

        static void PausedMessage(const std::string &audioId);

        static void ResumeMessage(const std::string &audioId);

    private:
        struct CachedSegment {
            AasSegment mSeg;
            std::string mSpeakerName;  // 解析后的说话人名称(确保Web和DB一致)
            uint64_t mExpireTimeMs {0};
        };

        struct InProgressSegment {
            AasSegment mSeg;
            std::string mSpeakerName;
        };

        void HandleRealtimeResult(const AasResult &result);

        void HandleOfflineFinal(const AasResult &result);

        void CacheAndEvictSegments(const std::vector<AasSegment> &segments);

        void FlushCacheToDb();

        void FlushSingleSegment(const CachedSegment &cached);

        bool SaveSegmentToDb(const AasSegment &seg, const std::string &speakerName, uint64_t &outId, bool &outSegFlag);

        bool DecideSegFlag(const std::string &speakerName, const std::string &content);

        void CompleteInProgressSegment();

        void PushToFrontend(const AasResult &result);

        // 加载声纹缓存(number→speaker映射)
        void LoadSpeakerCache();

        // 根据svEmbeddingMd5解析说话人名称(使用缓存)
        std::string ResolveSpeakerName(const std::string &speakerId, const std::string &defaultName);

    private:
        std::string mAudioId;
        uint64_t mAccountId {0};
        bool mIsRealtime {false};

        std::mutex mCacheMutex;
        std::map<int32_t, CachedSegment> mSegmentCache;
        std::atomic<bool> mDestroyed {false};

        InProgressSegment mInProgress;
        bool mHasInProgress {false};

        std::string mLastSpeaker;
        int mParagraphChars {0};

        // 声纹缓存: svEmbeddingMd5 → speaker名称
        std::unordered_map<std::string, std::string> mSpeakerCache;
        bool mSpeakerCacheLoaded {false};
        std::atomic<int32_t> mStrangerCounter {1};

        // 已完成段的时间边界(防止重复推送)
        int32_t mLastCompletedEndMs {-1};

        static constexpr int64_t CacheExpireMs = 2500;  // 2.5秒缓存过期
        // 分段区间 [TargetChars, MaxTargetChars),
        // 如果超过MaxTargetChars强制分段。不过如果转写出来的字段超过MaxTargetChars也没有办法了，这里只做简单的分段处理
        static constexpr int TargetChars = 200;
        static constexpr int MaxTargetChars = 500;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_TRANSCRIBE_HANDLER_H
