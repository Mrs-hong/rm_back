//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <memory>
#include <string>

#include "qifeng_framework/aas/aas_callback.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/audio/audio_utils.h"
#include "common/config/hal_config.h"
#include "common/config/hotword_config.h"
#include "common/voiceprint/feature_serialize.h"
#include "dao/hotword_dao.h"
#include "dao/speaker_dao.h"
#include "internal/aas/audio_transcribe_manager.h"
#include "internal/hal/hal_bridge.h"

namespace qifeng_ca {

    AudioStreamProvider::AudioStreamProvider(const std::string &audioId, uint64_t accountId, bool isRealtime,
                                             const Config &config)
        : BmsAudioProvider(audioId, accountId), mIsRealtime(isRealtime) {
        if (config.mFormatConfigSet) {
            mFormatConfig = config.mFormatConfig;
        } else {
            // 获取麦克风采样率、声道数、位深并推断时长
            auto audioFormat = HalBridge::GetInstance().GetAudioFormat();
            mFormatConfig.mSampleRate = audioFormat.mSampleRate;
            mFormatConfig.mChannels = audioFormat.mChannels;
            mFormatConfig.mBitDepth = audioFormat.mBitDepth;
        }

        mOneSecondBytes = CalcOneSecondBytes();

        mMaxSendSeconds = config.mMaxSendSeconds;
        mRedundancySeconds = config.mRedundancySeconds;
        mMaxBufferSeconds = config.mMaxBufferSeconds;

        SLOG_DEBUG << "AudioStreamProvider: create, audioId=" << mAudioId << ", accountId=" << mAccountId
                   << ", sampleRate=" << mFormatConfig.mSampleRate << ", channels=" << mFormatConfig.mChannels
                   << ", bitDepth=" << mFormatConfig.mBitDepth << "oneSecondBytes=" << mOneSecondBytes
                   << ", maxBufferSeconds=" << mMaxBufferSeconds << ", maxSendSeconds=" << mMaxSendSeconds
                   << ", redundancySeconds=" << mRedundancySeconds << ", isRealtime=" << mIsRealtime;

        mAudioBuffer.reserve(static_cast<size_t>(mMaxBufferSeconds) * mOneSecondBytes);

        LoadSpeakerDatabase();
        LoadHotwords();
    }

    AudioStreamProvider::~AudioStreamProvider() {
    }

    size_t AudioStreamProvider::CalcOneSecondBytes() const {
        AudioUtilsConfig config;
        config.mSampleRate = mFormatConfig.mSampleRate;
        config.mChannels = mFormatConfig.mChannels;
        config.mBitDepth = mFormatConfig.mBitDepth;
        return AudioUtils::CalculateOneSecondBytes(config);
    }

    std::shared_ptr<qifeng::aas::ResultInfo> AudioStreamProvider::GetAudio(bool isNonblock) {
        SLOG_DEBUG << "AudioStreamProvider: get audio, isNonblock=" << isNonblock << ", audioId=" << mAudioId;
        if (mIsRealtime) {
            return GetAudioRealtime(isNonblock);
        }
        return GetAudioOffline();
    }

    std::shared_ptr<qifeng::aas::ResultInfo> AudioStreamProvider::GetAudioRealtime(bool isNonblock) {
        std::unique_lock<std::mutex> lock(mBufferMutex);

        if (isNonblock) {
            if (mPaused.load() || !HasNewData()) {
                return nullptr;
            }
        } else {
            mBufferCv.wait(lock, [this] { return (!mPaused.load() && HasNewData()) || mEnded.load(); });
        }

        if (mPaused.load()) {
            return nullptr;
        }

        if (!HasNewData() && mEnded.load()) {
            return nullptr;
        }

        size_t currentEnd = mWriteOffset;
        size_t newBytes = currentEnd - mLastSentEnd;
        size_t alignedNew = (newBytes / mOneSecondBytes) * mOneSecondBytes;
        if (alignedNew == 0) {
            return nullptr;
        }

        size_t advanceBytes = std::min(alignedNew, static_cast<size_t>(mMaxSendSeconds) * mOneSecondBytes);
        size_t sendEnd = mLastSentEnd + advanceBytes;
        size_t sendStart = CalcSendStartOffsetFor(sendEnd);

        // 分片后从最后一个分片结束位置开始, 避免重复发送已截断数据
        size_t fragmentCut = mFragmentCutOffset.load();
        if (fragmentCut != 0 && fragmentCut > sendStart) {
            sendStart = fragmentCut;
        }

        if (sendEnd - sendStart < mOneSecondBytes) {
            return nullptr;
        }

        size_t startIdx = sendStart - mTrimOffset;
        size_t endIdx = sendEnd - mTrimOffset;
        auto reult = BuildResultInfoFromBuffer(startIdx, endIdx, sendStart, sendEnd);
        mLastSentEnd = sendEnd;
        // mFragmentCutOffset.store(0);
        TrimBufferIfNeeded();

        return reult;
    }

    std::shared_ptr<qifeng::aas::ResultInfo> AudioStreamProvider::GetAudioOffline() {
        // 优先使用DataPreparer回调方式(复用实时缓冲区冗余逻辑)
        if (mDataPreparer) {
            FeedOfflineDataToBuffer();
            return GetAudioRealtime(true);
        }
        SLOG_ERROR << "AudioStreamProvider: no data preparer set, audioId=" << mAudioId;
        return nullptr;
    }

    void AudioStreamProvider::FeedOfflineDataToBuffer() {
        if (mDataPreparerEof.load()) {
            SLOG_DEBUG << "AudioStreamProvider: data preparer eof, audioId=" << mAudioId;
            return;
        }

        // 每次从回调读取音频数据推入缓冲区
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

    void AudioStreamProvider::SetDataPreparer(DataPreparer preparer) {
        SLOG_DEBUG << "AudioStreamProvider: set data preparer, audioId=" << mAudioId;
        mDataPreparer = std::move(preparer);
    }

    void AudioStreamProvider::PushAudioData(const uint8_t* data, size_t len) {
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

    void AudioStreamProvider::Pause() {
        mPaused.store(true);
        mBufferCv.notify_all();
        SLOG_INFO << "AudioStreamProvider: paused, audioId=" << mAudioId;
    }

    void AudioStreamProvider::Resume() {
        mPaused.store(false);
        mBufferCv.notify_all();
        SLOG_INFO << "AudioStreamProvider: resumed, audioId=" << mAudioId;
    }

    void AudioStreamProvider::SignalEnd() {
        {
            std::lock_guard<std::mutex> lock(mBufferMutex);
            mEnded.store(true);
        }
        mBufferCv.notify_all();
        SLOG_INFO << "AudioStreamProvider: audio stream ended, audioId=" << mAudioId;
    }

    bool AudioStreamProvider::HasNewData() const {
        size_t newBytes = mWriteOffset - mLastSentEnd;
        return newBytes >= mOneSecondBytes;
    }

    bool AudioStreamProvider::HasPendingData() const {
        std::lock_guard<std::mutex> lock(mBufferMutex);
        return HasNewData();
    }

    size_t AudioStreamProvider::CalcSendStartOffsetFor(size_t sendEnd) const {
        size_t redundancyBytes = static_cast<size_t>(mRedundancySeconds) * mOneSecondBytes;
        size_t start = 0;
        if (sendEnd > redundancyBytes) {
            start = sendEnd - redundancyBytes;
        }
        if (start < mTrimOffset) {
            start = mTrimOffset;
        }
        return start;
    }

    qifeng::aas::AasResult AudioStreamProvider::RebuildSegments(qifeng::aas::AasResult src, int redundancySeconds,
                                                                const std::string audioId) {
        // 合并完全连续的段: 前一段 isFullSegment=false 且 endTime==后一段 startTime 时合并
        //    多个连续段都需要合并, 说话人信息取后一段, text 直接拼接
        qifeng::aas::AasResult merged = src;
        merged.segments.clear();
        for (const auto &seg : src.segments) {
            if (!merged.segments.empty()) {
                auto &last = merged.segments.back();
                if (!last.isFullSegment && last.endTime == seg.startTime) {
                    last.endTime = seg.endTime;
                    last.text += seg.text;
                    last.speakerLabel = seg.speakerLabel;
                    last.speakerName = seg.speakerName;
                    last.svEmbedding = seg.svEmbedding;
                    last.svEmbeddingMd5 = seg.svEmbeddingMd5;
                    last.isFullSegment = seg.isFullSegment;
                    continue;
                }
                // 强制分段（aas没有做到切分，这里手动切分）
                if (!last.isFullSegment && last.endTime != seg.startTime) {
                    SLOG_DEBUG << "AudioStreamProvider: rebuild segments, not continuous segment, audioId=" << audioId
                               << ", last.startTime=" << last.startTime << ", last.endTime=" << last.endTime
                               << ", text=" << last.text << ", seg.startTime=" << seg.startTime
                               << ", seg.endTime=" << seg.endTime << ", text=" << seg.text;
                    last.isFullSegment = true;
                }
            }
            merged.segments.push_back(seg);
        }

        // 强制分段: 段时长超过 redundancySeconds 则将 isFullSegment 改为 true
        int64_t redundancyMs = static_cast<int64_t>(redundancySeconds) * 1000;
        for (auto &seg : merged.segments) {
            if (seg.endTime - seg.startTime > redundancyMs) {
                seg.isFullSegment = true;
            }
        }
        return merged;
    }

    void AudioStreamProvider::TrimBufferIfNeeded() {
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

    std::shared_ptr<qifeng::aas::ResultInfo>
    AudioStreamProvider::BuildResultInfoFromBuffer(size_t startIdx, size_t endIdx, size_t sendStart, size_t sendEnd) {
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
    AudioStreamProvider::BuildResultInfoFromData(std::vector<uint8_t> &audioData, size_t sendStart, size_t sendEnd) {
        std::string capturedAudioId = mAudioId;
        uint64_t capturedAccountId = mAccountId;
        auto capturedCb = mTransResultCb;

        qifeng::aas::ResultInfo::SendCallBack callback = [this, capturedAudioId, capturedAccountId,
                                                          capturedCb](qifeng::aas::AasResult aasResult,
                                                                      qifeng::aas::BmsInfo bmsInfo) {
            SLOG_DEBUG << "AudioStreamProvider audioId=" << mAudioId << ", startTime=" << mTestStartTime
                       << ", endTime=" << mTestEndTime << ", data size=" << aasResult.segments.size();
            // 重构 AasResult: 合并连续段 + 超时长强制分段
            // mRedundancySeconds - 5：默认认为5s是aas最多处理时间，所以时间上进行修正
            auto mergedResult = RebuildSegments(std::move(aasResult), mRedundancySeconds - 5, mAudioId);

            // 基于重构后的 segments 计算最后一个分片结束位置, 用于下次发送裁剪冗余
            int64_t lastFragmentEndMs = -1;
            int64_t start = 0;
            int64_t end = 0;
            for (const auto &seg : mergedResult.segments) {
                if (seg.isFullSegment && seg.endTime > lastFragmentEndMs) {
                    lastFragmentEndMs = seg.endTime;
                    start = seg.startTime;
                    end = seg.endTime;
                }
            }
            if (lastFragmentEndMs >= 0) {
                size_t cutOffset = static_cast<size_t>(lastFragmentEndMs) * mOneSecondBytes / 1000;
                if (cutOffset > mFragmentCutOffset.load()) {
                    mFragmentCutOffset.store(cutOffset);
                    SLOG_INFO << "AudioStreamProvider: fragment detected, cutOffset=" << cutOffset
                              << " lastFragmentEndMs=" << lastFragmentEndMs << " chunk=[" << start << "," << end << "]"
                              << ", audioId=" << capturedAudioId << ", mFragmentCutOffset=" << cutOffset;
                }
            }

            if (capturedCb) {
                capturedCb(capturedAudioId, capturedAccountId, mergedResult);
                SLOG_INFO << "AudioStreamProvider: AAS callback done";
            } else {
                SLOG_ERROR << "AudioStreamProvider: AAS callback is NULL";
            }

            (void)bmsInfo;
        };

        qifeng::aas::BmsInfo bmsInfo {};
        bmsInfo.mAudioId = mAudioId;
        // 毫秒级精度: 先乘1000再除, 避免秒级截断丢失亚秒精度
        bmsInfo.mStartTime = static_cast<int64_t>(sendStart * 1000 / mOneSecondBytes);
        bmsInfo.mEndTime = static_cast<int64_t>(sendEnd * 1000 / mOneSecondBytes);
        mTestStartTime = bmsInfo.mStartTime;
        mTestEndTime = bmsInfo.mEndTime;

        SLOG_DEBUG << "BuildResultInfoFromData audioId=" << mAudioId << ", startTime=" << bmsInfo.mStartTime
                   << ", endTime=" << bmsInfo.mEndTime << ", data size=" << audioData.size();

        return std::make_shared<qifeng::aas::ResultInfo>(qifeng::aas::ResultInfo {
            callback, std::move(audioData), mFormatConfig, bmsInfo, mSvDataBase, mHotwords, mAudioId});
    }

    void AudioStreamProvider::LoadSpeakerDatabase() {
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

        SLOG_INFO << "AudioStreamProvider: loaded " << mSvDataBase->svDatabase.size()
                  << " speakers for accountId=" << mAccountId;
    }

    void AudioStreamProvider::LoadHotwords() {
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

        SLOG_INFO << "AudioStreamProvider: loaded " << mHotwords->hotwords.size()
                  << " hotwords for accountId=" << mAccountId;
    }

}  // namespace qifeng_ca
