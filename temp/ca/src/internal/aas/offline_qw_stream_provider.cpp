//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <memory>
#include <string>

#include "qifeng_framework/aas/aas_callback.h"
#include "qifeng_framework/common/logger.h"

#include "common/audio/audio_utils.h"
#include "common/config/hotword_config.h"
#include "common/voiceprint/feature_serialize.h"
#include "dao/hotword_dao.h"
#include "dao/speaker_dao.h"
#include "internal/aas/offline_qw_stream_provider.h"

namespace qifeng_ca {

    static constexpr int ChunkSeconds = 30;  // 固定30s分片

    OfflineQWStreamProvider::OfflineQWStreamProvider(const std::string &audioId, uint64_t accountId,
                                                     const Config &config)
        : BmsAudioProvider(audioId, accountId) {
        if (config.mFormatConfigSet) {
            mFormatConfig = config.mFormatConfig;
        } else {
            // 默认16kHz 16bit 单声道
            mFormatConfig.mSampleRate = 16000;
            mFormatConfig.mChannels = 1;
            mFormatConfig.mBitDepth = 16;
        }

        mOneSecondBytes = CalcOneSecondBytes();
        mChunkSendBytes = mOneSecondBytes * static_cast<size_t>(ChunkSeconds);
        mMaxBufferSeconds = config.mMaxBufferSeconds;

        SLOG_DEBUG << "OfflineQWStreamProvider: create, audioId=" << mAudioId << ", accountId=" << mAccountId
                   << ", sampleRate=" << mFormatConfig.mSampleRate << ", channels=" << mFormatConfig.mChannels
                   << ", bitDepth=" << mFormatConfig.mBitDepth << ", oneSecondBytes=" << mOneSecondBytes
                   << ", chunkSendBytes=" << mChunkSendBytes << ", maxBufferSeconds=" << mMaxBufferSeconds;

        mAudioBuffer.reserve(static_cast<size_t>(mMaxBufferSeconds) * mOneSecondBytes);

        LoadSpeakerDatabase();
        LoadHotwords();
    }

    OfflineQWStreamProvider::~OfflineQWStreamProvider() = default;

    size_t OfflineQWStreamProvider::CalcOneSecondBytes() const {
        AudioUtilsConfig config;
        config.mSampleRate = mFormatConfig.mSampleRate;
        config.mChannels = mFormatConfig.mChannels;
        config.mBitDepth = mFormatConfig.mBitDepth;
        return AudioUtils::CalculateOneSecondBytes(config);
    }

    std::shared_ptr<qifeng::aas::ResultInfo> OfflineQWStreamProvider::GetAudio(bool isNonblock) {
        (void)isNonblock;
        // 离线模式: 始终从DataPreparer喂数据后非阻塞读取
        if (!mDataPreparer) {
            SLOG_ERROR << "OfflineQWStreamProvider: no data preparer set, audioId=" << mAudioId;
            return nullptr;
        }
        FeedOfflineDataToBuffer();
        return GetAudioOffline();
    }

    void OfflineQWStreamProvider::FeedOfflineDataToBuffer() {
        if (mDataPreparerEof.load()) {
            return;
        }

        std::vector<uint8_t> chunk;
        bool hasMore = mDataPreparer(chunk);

        if (!chunk.empty()) {
            PushAudioData(chunk.data(), chunk.size());
        }

        if (!hasMore) {
            mDataPreparerEof.store(true);
            SignalEnd();
        }
    }

    void OfflineQWStreamProvider::SetDataPreparer(DataPreparer preparer) {
        SLOG_DEBUG << "OfflineQWStreamProvider: set data preparer, audioId=" << mAudioId;
        mDataPreparer = std::move(preparer);
    }

    std::shared_ptr<qifeng::aas::ResultInfo> OfflineQWStreamProvider::GetAudioOffline() {
        std::lock_guard<std::mutex> lock(mBufferMutex);

        if (!HasNewData() && mEnded.load()) {
            return nullptr;
        }

        if (!HasNewData()) {
            return nullptr;
        }

        // 固定10s分片: 从上次发送结束位置开始, 无冗余不重复
        size_t sendStart = mLastSentEnd;
        size_t sendEnd = sendStart + mChunkSendBytes;
        if (sendEnd > mWriteOffset) {
            sendEnd = mWriteOffset;
        }

        size_t startIdx = sendStart - mTrimOffset;
        size_t endIdx = sendEnd - mTrimOffset;
        auto result = BuildResultInfoFromBuffer(startIdx, endIdx, sendStart, sendEnd);
        mLastSentEnd = sendEnd;

        // 分片序号递增, 用于传输连续性检测
        ++mChunkSequence;

        TrimBufferIfNeeded();
        return result;
    }

    void OfflineQWStreamProvider::PushAudioData(const uint8_t* data, size_t len) {
        if (!data || len == 0) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mBufferMutex);
            if (mEnded.load()) {
                return;
            }
            mAudioBuffer.insert(mAudioBuffer.end(), data, data + len);
            mWriteOffset += len;
        }
        mBufferCv.notify_one();
    }

    void OfflineQWStreamProvider::SignalEnd() {
        {
            std::lock_guard<std::mutex> lock(mBufferMutex);
            mEnded.store(true);
        }
        mBufferCv.notify_all();
        SLOG_INFO << "OfflineQWStreamProvider: audio stream ended, audioId=" << mAudioId;
    }

    bool OfflineQWStreamProvider::HasNewData() const {
        size_t newBytes = mWriteOffset - mLastSentEnd;
        return newBytes >= mOneSecondBytes;
    }

    bool OfflineQWStreamProvider::HasPendingData() const {
        std::lock_guard<std::mutex> lock(mBufferMutex);
        return HasNewData();
    }

    void OfflineQWStreamProvider::TrimBufferIfNeeded() {
        size_t maxBufferBytes = static_cast<size_t>(mMaxBufferSeconds) * mOneSecondBytes;
        if (mAudioBuffer.size() <= maxBufferBytes) {
            return;
        }

        size_t trimSize = mAudioBuffer.size() - maxBufferBytes;
        mAudioBuffer.erase(mAudioBuffer.begin(), mAudioBuffer.begin() + static_cast<ptrdiff_t>(trimSize));
        mTrimOffset += trimSize;

        if (mLastSentEnd < mTrimOffset) {
            mLastSentEnd = mTrimOffset;
        }
    }

    std::shared_ptr<qifeng::aas::ResultInfo> OfflineQWStreamProvider::BuildResultInfoFromBuffer(size_t startIdx,
                                                                                                size_t endIdx,
                                                                                                size_t sendStart,
                                                                                                size_t sendEnd) {
        if (endIdx > mAudioBuffer.size()) {
            endIdx = mAudioBuffer.size();
        }
        if (startIdx >= endIdx) {
            return nullptr;
        }

        std::vector<uint8_t> audioData(mAudioBuffer.begin() + static_cast<ptrdiff_t>(startIdx),
                                       mAudioBuffer.begin() + static_cast<ptrdiff_t>(endIdx));
        return BuildResultInfoFromData(audioData, sendStart, sendEnd);
    }

    std::shared_ptr<qifeng::aas::ResultInfo>
    OfflineQWStreamProvider::BuildResultInfoFromData(std::vector<uint8_t> &audioData, size_t sendStart,
                                                     size_t sendEnd) {
        std::string capturedAudioId = mAudioId;
        uint64_t capturedAccountId = mAccountId;
        auto capturedCb = mTransResultCb;

        qifeng::aas::ResultInfo::SendCallBack callback = [this, capturedAudioId, capturedAccountId,
                                                          capturedCb](qifeng::aas::AasResult aasResult,
                                                                      qifeng::aas::BmsInfo bmsInfo) {
            SLOG_DEBUG << "OfflineQWStreamProvider audioId=" << mAudioId << ", sentStart=" << mTestStartTime
                       << "ms, sentEnd=" << mTestEndTime << "ms, segments=" << aasResult.segments.size();

            if (capturedCb) {
                capturedCb(capturedAudioId, capturedAccountId, aasResult);
                SLOG_INFO << "OfflineQWStreamProvider: AAS callback done";
            } else {
                SLOG_ERROR << "OfflineQWStreamProvider: AAS callback is NULL";
            }
            (void)bmsInfo;
        };

        qifeng::aas::BmsInfo bmsInfo {};
        bmsInfo.mAudioId = mAudioId;
        bmsInfo.mStartTime = static_cast<int64_t>(sendStart * 1000 / mOneSecondBytes);
        bmsInfo.mEndTime = static_cast<int64_t>(sendEnd * 1000 / mOneSecondBytes);
        mTestStartTime = bmsInfo.mStartTime;
        mTestEndTime = bmsInfo.mEndTime;
        bmsInfo.isOnline = false;

        SLOG_DEBUG << "BuildResultInfoFromData audioId=" << mAudioId << ", startTime=" << bmsInfo.mStartTime
                   << ", endTime=" << bmsInfo.mEndTime << ", dataSize=" << audioData.size();

        return std::make_shared<qifeng::aas::ResultInfo>(qifeng::aas::ResultInfo {
            callback, std::move(audioData), mFormatConfig, bmsInfo, mSvDataBase, mHotwords, mAudioId});
    }

    void OfflineQWStreamProvider::LoadSpeakerDatabase() {
        mSvDataBase = std::make_shared<qifeng::aas::SvDataBase>();

        SpeakerDao dao;
        SpeakerSearchFilter filter;
        filter.mAccountId = mAccountId;
        filter.mPageSize = 10000;
        auto result = dao.Search(filter);

        for (const auto &speaker : result.mRecords) {
            if (speaker.mFeatures.empty()) {
                continue;
            }

            auto embedding2d = DeserializeEmbedding(speaker.mFeatures, speaker.mDim);
            if (embedding2d.empty() || embedding2d[0].empty()) {
                continue;
            }
            const auto &embedding = embedding2d[0];

            std::string key = speaker.mSpeakerId.empty() ? speaker.mSpeakerName : speaker.mSpeakerId;
            mSvDataBase->svDatabase[key].emplace_back(embedding);
        }

        SLOG_INFO << "OfflineQWStreamProvider: loaded " << mSvDataBase->svDatabase.size()
                  << " speakers for accountId=" << mAccountId;
    }

    void OfflineQWStreamProvider::LoadHotwords() {
        mHotwords = std::make_shared<qifeng::aas::HotWords>();

        // 不需要热词功能
        // HotwordDao dao;
        // HotwordSearchFilter filter;
        // filter.mAccountId = mAccountId;
        // filter.mStatus = 1;
        // filter.mPageSize = HotWordConfig::GetInstance().GetHotwordMaxLimit();
        // auto result = dao.Search(filter);

        // for (const auto &hw : result.mRecords) {
        //     mHotwords->hotwords.push_back(hw.mWord);
        // }

        SLOG_INFO << "OfflineQWStreamProvider: loaded " << mHotwords->hotwords.size()
                  << " hotwords for accountId=" << mAccountId;
    }

}  // namespace qifeng_ca
