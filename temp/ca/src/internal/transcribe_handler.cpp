//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <common/utils/time.h>
#include <cstdint>
#include <string>
#include <vector>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"
#include "utf8/checked.h"

#include "common/ws/realtime_dispatch_manager.h"
#include "dao/speaker_dao.h"
#include "dao/trans_dao.h"
#include "internal/transcribe_handler.h"

namespace qifeng_ca {

    static std::vector<AasSegment> ConvertAasSegments(const std::vector<qifeng::aas::AasSegment> &src) {
        std::vector<AasSegment> dst;
        dst.reserve(src.size());

        int64_t start = 0;
        int64_t end = 0;
        for (const auto &seg : src) {
            AasSegment bmsSeg;
            bmsSeg.mStartMs = static_cast<int32_t>(seg.startTime);
            bmsSeg.mEndMs = static_cast<int32_t>(seg.endTime);
            bmsSeg.mText = seg.text;
            bmsSeg.mSpeakerId = seg.svEmbeddingMd5;
            bmsSeg.mSpeakerName = "陌生人1";
            // bmsSeg.mSpeakerName = seg.speakerName;
            bmsSeg.mIsFullSegment = seg.isFullSegment;
            if (start == 0 || start > bmsSeg.mStartMs) {
                start = bmsSeg.mStartMs;
            }
            if (end < bmsSeg.mEndMs) {
                end = bmsSeg.mEndMs;
            }
            dst.push_back(bmsSeg);
            SLOG_DEBUG << "segments start time: " << bmsSeg.mStartMs << ", end time: " << bmsSeg.mEndMs
                       << ", speaker id: " << bmsSeg.mSpeakerId << ", mSpeakerLabel: " << bmsSeg.mSpeakerLabel
                       << ", speaker name: " << bmsSeg.mSpeakerName << ", tran text: " << bmsSeg.mText
                       << ", IsFullSegment: " << bmsSeg.mIsFullSegment;
        }
        SLOG_DEBUG << "segments size: " << dst.size() << ", start time: " << start << ", end time: " << end;

        std::sort(dst.begin(), dst.end(),
                  [](const AasSegment &a, const AasSegment &b) { return a.mStartMs < b.mStartMs; });

        return dst;
    }

    static AasResult ConvertAasResult(const qifeng::aas::AasResult &aasResult) {
        AasResult bmsResult;
        bmsResult.mCode = aasResult.code;
        bmsResult.mMessage = aasResult.message;
        bmsResult.mTraceId = aasResult.traceId;
        bmsResult.mText = aasResult.text;
        bmsResult.mProcessingTimeMs = 0;
        bmsResult.mSpeakerLabel = aasResult.speakerLabel;
        bmsResult.mSegments = ConvertAasSegments(aasResult.segments);

        SLOG_DEBUG << "tran text: " << aasResult.text;
        return bmsResult;
    }

    static std::string BuildSegmentMessage(const std::string &audioId, const AasSegment &seg,  // NOLINT
                                           const std::string &speakerName, uint64_t id, bool segFlag, bool isFinal) {
        std::string json = R"({"heart":false,)";
        if (id == 0) {
            json += R"("id":null,)";
        } else {
            json += R"("id":)" + std::to_string(id) + R"(,)";
        }
        json += R"("aid":")" + audioId + R"(","speaker_name":")" + speakerName + R"(","start_time":)" +
                std::to_string(seg.mStartMs) + R"(,"end_time":)" + std::to_string(seg.mEndMs) + R"(,"content":")" +
                seg.mText + R"(","seg_flag":)" + (segFlag ? "true" : "false") + R"(,"final":)" +
                (isFinal ? "true" : "false") + "}";
        SLOG_DEBUG << "BuildSegmentMessage: " << json << ", mText: " << seg.mText;
        return json;
    }

    static std::string BuildFinalMessage(const std::string &audioId) {
        return R"({"heart":false,"id":null,"aid":")" + audioId +
               R"(","speaker_name":null,"start_time":null,"end_time":null,"content":null,"seg_flag":null,"final":true})";
    }

    static void PushToClients(const std::string &audioId, const std::string &message) {
        auto &dispatchMgr = RealtimeDispatchManager::GetInstance();
        bool ok = dispatchMgr.Distribute(message, EClientType::kTranscribe);
        if (!ok) {
            SLOG_DEBUG << "TranscribeHandler: no ws clients for audioId=" << audioId;
        }
    }

    static bool SegmentsOverlap(int32_t startA, int32_t endA, int32_t startB, int32_t endB) {
        int32_t overlapStart = std::max(startA, startB);
        int32_t overlapEnd = std::min(endA, endB);
        return overlapStart < overlapEnd;
    }
    struct OverlapCheckResult {
        bool mIsDominated {false};
        std::vector<uint64_t> mDominatedIds;
    };

    // 检查是否被DB中的记录覆盖并返回被覆盖的记录ID
    static OverlapCheckResult FindDominatedDbRecords(const AasSegment &seg,
                                                     const std::vector<models::Trans> &existing) {
        int32_t newDuration = seg.mEndMs - seg.mStartMs;
        OverlapCheckResult result;

        for (const auto &old : existing) {
            if (old.mIsDiscard != 0) {
                continue;
            }
            if (!SegmentsOverlap(seg.mStartMs, seg.mEndMs, old.mStartTime, old.mEndTime)) {
                continue;
            }
            int32_t oldDuration = old.mEndTime - old.mStartTime;
            if (oldDuration >= newDuration) {
                result.mIsDominated = true;
                return result;
            }
            result.mDominatedIds.push_back(old.mId);
        }

        return result;
    }

    static bool IsIdInList(uint64_t id, const std::vector<uint64_t> &ids) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }

    static void LogDominatedRecords(uint64_t accountId, const std::string &audioId,
                                    const std::vector<models::Trans> &existing,
                                    const std::vector<uint64_t> &dominatedIds) {
        for (const auto &rec : existing) {
            if (!IsIdInList(rec.mId, dominatedIds)) {
                continue;
            }
            SLOG_WARN << "TranscribeHandler: hard delete overlapping record, audioId=" << audioId
                      << " accountId=" << accountId << " id=" << rec.mId << " [" << rec.mStartTime << ","
                      << rec.mEndTime << "] content=" << rec.mContent;
        }
    }

    void TranscribeHandler::PausedMessage(const std::string &audioId) {
        std::string msg = R"({"heart":false,"id":null,"aid":")" + audioId + R"(","pause":true})";
        PushToClients(audioId, msg);
    }

    void TranscribeHandler::ResumeMessage(const std::string &audioId) {
        std::string msg = R"({"heart":false,"id":null,"aid":")" + audioId + R"(","resume":true})";
        PushToClients(audioId, msg);
    }

    TranscribeHandler::TranscribeHandler(const std::string &audioId, uint64_t accountId, bool isRealtime)
        : mAudioId(audioId), mAccountId(accountId), mIsRealtime(isRealtime) {
        LoadSpeakerCache();
        SLOG_INFO << "TranscribeHandler: created, audioId=" << audioId << " isRealtime=" << isRealtime;
    }

    TranscribeHandler::~TranscribeHandler() {
        mDestroyed.store(true);
        CachedSegment lastSeg;
        lastSeg.mExpireTimeMs = 0;
        lastSeg.mSeg = std::move(mInProgress.mSeg);
        lastSeg.mSpeakerName = std::move(mInProgress.mSpeakerName);
        mSegmentCache[lastSeg.mSeg.mStartMs] = std::move(lastSeg);

        FlushCacheToDb();
        NotifyFinal();
        SLOG_INFO << "TranscribeHandler: destroyed, audioId=" << mAudioId;
    }

    void TranscribeHandler::OnAasResult(const qifeng::aas::AasResult &result) {
        if (mDestroyed.load()) {
            SLOG_WARN << "TranscribeHandler: OnAasResult after destroy, audioId=" << mAudioId;
            return;
        }

        // AAS模型输出可能含非法UTF-8(尤其尾部短数据), 处理链路可能抛异常,
        // 此处兜底捕获, 防止异常逃逸出AAS线程导致进程崩溃
        try {
            SLOG_INFO << "TranscribeHandler: OnAasResult, audioId=" << mAudioId << " code=" << result.code;

            AasResult bmsResult = ConvertAasResult(result);

            if (bmsResult.mCode != 0) {
                SLOG_ERROR << "TranscribeHandler: AAS result error, code=" << bmsResult.mCode
                           << " msg=" << bmsResult.mMessage;
                return;
            }

            if (mIsRealtime) {
                HandleRealtimeResult(bmsResult);
            } else {
                HandleOfflineFinal(bmsResult);
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "TranscribeHandler: OnAasResult exception, audioId=" << mAudioId << " err=" << e.what();
        } catch (...) {
            SLOG_ERROR << "TranscribeHandler: OnAasResult unknown exception, audioId=" << mAudioId;
        }
    }

    void TranscribeHandler::HandleRealtimeResult(const AasResult &result) {
        PushToFrontend(result);
    }

    void TranscribeHandler::HandleOfflineFinal(const AasResult &result) {
        if (!result.mSegments.empty()) {
            CacheAndEvictSegments(result.mSegments);
        }

        SLOG_INFO << "TranscribeHandler: offline transcribe final, audioId=" << mAudioId
                  << " segments=" << result.mSegments.size();
    }

    void TranscribeHandler::LoadSpeakerCache() {
        if (mSpeakerCacheLoaded) {
            return;
        }

        SpeakerDao dao;
        auto speakers = dao.ListByAccountId(mAccountId);
        for (const auto &sp : speakers) {
            if (!sp.mSpeakerId.empty() && !sp.mSpeakerName.empty()) {
                mSpeakerCache[sp.mSpeakerId] = sp.mSpeakerName;
            }
        }
        mSpeakerCacheLoaded = true;
        SLOG_INFO << "TranscribeHandler: speaker cache loaded, accountId=" << mAccountId
                  << " count=" << mSpeakerCache.size();
    }

    std::string TranscribeHandler::ResolveSpeakerName(const std::string &speakerId, const std::string &defaultName) {
        // defaultName来自AAS聚合的svResult, 即svDatabase的key
        // 若命中已知声纹则为注册声纹的MD5(mSpeakerId), 优先用它查缓存/DB
        if (!defaultName.empty() && defaultName.compare(0, 7, "陌生人") != 0) {
            auto it = mSpeakerCache.find(defaultName);
            if (it != mSpeakerCache.end()) {
                return it->second;
            }
            SpeakerDao dao;
            auto speaker = dao.GetBySpeakerId(mAccountId, defaultName);
            if (speaker.mId != 0 && !speaker.mSpeakerName.empty()) {
                mSpeakerCache[defaultName] = speaker.mSpeakerName;
                return speaker.mSpeakerName;
            }
            return defaultName;
        }

        // 未匹配已知声纹(陌生人)或为空, 用当前段embedding的MD5查缓存/DB
        if (!speakerId.empty()) {
            auto it = mSpeakerCache.find(speakerId);
            if (it != mSpeakerCache.end()) {
                return it->second;
            }
            SpeakerDao dao;
            auto speaker = dao.GetBySpeakerId(mAccountId, speakerId);
            if (speaker.mId != 0 && !speaker.mSpeakerName.empty()) {
                mSpeakerCache[speakerId] = speaker.mSpeakerName;
                return speaker.mSpeakerName;
            }
        }

        if (speakerId.empty()) {
            return "陌生人 " + std::to_string(mStrangerCounter.fetch_add(1));
        }
        std::string name = "陌生人 " + std::to_string(mStrangerCounter.fetch_add(1));
        mSpeakerCache[speakerId] = name;
        SLOG_INFO << "TranscribeHandler: new stranger assigned, speakerId=" << speakerId << " name=" << name;
        return name;
    }

    void TranscribeHandler::NotifyFinal() {
        std::string msg = BuildFinalMessage(mAudioId);
        PushToClients(mAudioId, msg);

        SLOG_INFO << "TranscribeHandler: NotifyFinal, audioId=" << mAudioId;
    }

    void TranscribeHandler::PushToFrontend(const AasResult &result) {
        for (const auto &seg : result.mSegments) {
            // 时序保护: 跳过已完成段时间范围内的重复数据
            if (seg.mText.empty()) {
                continue;
            }
            if (mLastCompletedEndMs >= 0 && seg.mStartMs < mLastCompletedEndMs) {
                SLOG_DEBUG << "TranscribeHandler: skip already completed segment, audioId=" << mAudioId << " ["
                           << seg.mStartMs << "," << seg.mEndMs << "]" << " mLastCompletedEndMs=" << mLastCompletedEndMs
                           << " seg.txt=" << seg.mText;
                continue;
            }

            std::string speakerName = ResolveSpeakerName(seg.mSpeakerId, seg.mSpeakerName);
            SLOG_DEBUG << "[test] audioId=" << mAudioId << " speakerId=" << seg.mSpeakerId
                       << " speakerName=" << speakerName << " startMs=" << seg.mStartMs << " endMs=" << seg.mEndMs
                       << " seg.txt=" << seg.mText << " mHasInProgress=" << mHasInProgress
                       << " IsFullSegment=" << seg.mIsFullSegment;

            // 分片标志位优先划分段落, mStartMs变化作为兜底策略
            // if (mHasInProgress && (seg.mIsFullSegment || seg.mStartMs != mInProgress.mSeg.mStartMs)) {
            mInProgress.mSeg = seg;
            mInProgress.mSpeakerName = speakerName;
            mHasInProgress = true;
            if (mHasInProgress && seg.mIsFullSegment) {
                CompleteInProgressSegment();
            } else {
                std::string partialMsg = BuildSegmentMessage(mAudioId, seg, speakerName, 0, false, false);
                PushToClients(mAudioId, partialMsg);
            }
        }
    }

    void TranscribeHandler::CompleteInProgressSegment() {
        if (!mHasInProgress) {
            return;
        }

        uint64_t dbId = 0;
        bool segFlag = false;
        bool saved = SaveSegmentToDb(mInProgress.mSeg, mInProgress.mSpeakerName, dbId, segFlag);
        if (saved && dbId != 0) {
            std::string completeMsg =
                BuildSegmentMessage(mAudioId, mInProgress.mSeg, mInProgress.mSpeakerName, dbId, segFlag, false);
            PushToClients(mAudioId, completeMsg);
        }

        // 更新已完成段时间边界
        mLastCompletedEndMs = mInProgress.mSeg.mEndMs;
        mHasInProgress = false;
    }

    void TranscribeHandler::CacheAndEvictSegments(const std::vector<AasSegment> &segments) {
        auto now = GetTimeMs();
        auto expireTime = GetTimeMs() + CacheExpireMs;

        {
            std::lock_guard<std::mutex> lock(mCacheMutex);

            for (const auto &seg : segments) {
                CachedSegment cached;
                cached.mSeg = seg;
                cached.mSpeakerName = ResolveSpeakerName(seg.mSpeakerId, seg.mSpeakerName);
                cached.mExpireTimeMs = expireTime;
                mSegmentCache[seg.mStartMs] = std::move(cached);
            }

            for (auto it = mSegmentCache.begin(); it != mSegmentCache.end();) {
                if (it->second.mExpireTimeMs <= now) {
                    FlushSingleSegment(it->second);
                    it = mSegmentCache.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    void TranscribeHandler::FlushCacheToDb() {
        std::map<int32_t, CachedSegment> localCache;

        {
            std::lock_guard<std::mutex> lock(mCacheMutex);
            localCache = std::move(mSegmentCache);
            mSegmentCache.clear();
        }

        for (const auto &[key, cached] : localCache) {
            FlushSingleSegment(cached);
        }
    }

    void TranscribeHandler::FlushSingleSegment(const CachedSegment &cached) {
        const auto &seg = cached.mSeg;
        const std::string &speakerName = cached.mSpeakerName;

        uint64_t dbId = 0;
        bool segFlag = false;
        bool saved = SaveSegmentToDb(seg, speakerName, dbId, segFlag);
        if (!saved) {
            SLOG_ERROR << "TranscribeHandler: flush segment failed, audioId=" << mAudioId << " [" << seg.mStartMs << ","
                       << seg.mEndMs << "]";
        }
    }

    bool TranscribeHandler::DecideSegFlag(const std::string &speakerName, const std::string &content) {
        bool speakerChanged = !mLastSpeaker.empty() && speakerName != mLastSpeaker;
        bool charsExceeded = mParagraphChars > TargetChars;

        // 安全计算UTF-8字符数: AAS模型输出可能含非法UTF-8序列(尤其尾部短数据易产生截断字符),
        // checked 版本 utf8::distance 会抛 not_enough_room 异常导致进程崩溃,
        // 先用 replace_invalid 将非法序列替换为 U+FFFD 再计算
        std::string safeContent;
        utf8::replace_invalid(content.begin(), content.end(), std::back_inserter(safeContent));
        int charCount = static_cast<int>(utf8::distance(safeContent.begin(), safeContent.end()));
        auto currChars = mParagraphChars + charCount;
        bool segFlag = false;

        if (speakerChanged || charsExceeded || currChars >= MaxTargetChars) {
            segFlag = true;
            mParagraphChars = charCount;
        } else {
            mParagraphChars = currChars;
        }

        mLastSpeaker = speakerName;
        return segFlag;
    }

    bool TranscribeHandler::SaveSegmentToDb(const AasSegment &seg, const std::string &speakerName, uint64_t &outId,
                                            bool &outSegFlag) {
        TransDao dao;
        auto existing = dao.GetByAudioId(mAccountId, mAudioId);

        auto overlapResult = FindDominatedDbRecords(seg, existing);
        if (overlapResult.mIsDominated) {
            SLOG_DEBUG << "TranscribeHandler: segment dominated, skip, audioId=" << mAudioId << " [" << seg.mStartMs
                       << "," << seg.mEndMs << "]";
            outId = 0;
            outSegFlag = false;
            return true;
        }

        if (!overlapResult.mDominatedIds.empty()) {
            LogDominatedRecords(mAccountId, mAudioId, existing, overlapResult.mDominatedIds);
            dao.DeleteByIds(mAccountId, overlapResult.mDominatedIds);
        }

        outSegFlag = DecideSegFlag(speakerName, seg.mText);

        models::Trans newTrans;
        newTrans.mAccountId = mAccountId;
        newTrans.mAudioId = mAudioId;
        newTrans.mStartTime = seg.mStartMs;
        newTrans.mEndTime = seg.mEndMs;
        newTrans.mSpeakerName = speakerName;
        newTrans.mContent = seg.mText;
        newTrans.mIsOrig = 1;
        newTrans.mIsDiscard = 0;
        newTrans.mSegFlag = outSegFlag ? 1 : 0;

        bool ok = dao.Insert(newTrans);
        outId = ok ? newTrans.mId : 0;
        return ok;
    }

}  // namespace qifeng_ca
