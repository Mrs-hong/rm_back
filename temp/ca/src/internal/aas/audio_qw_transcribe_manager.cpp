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
#include "internal/aas/audio_qw_transcribe_manager.h"
#include "internal/aas/audio_transcribe_manager.h"
#include "internal/hal/hal_bridge.h"

namespace qifeng_ca {

    AudioQWStreamProvider::AudioQWStreamProvider(const std::string &audioId, uint64_t accountId, bool isRealtime,
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
        // 累计30s数据后调用一次QWen接口(可通过Config按任务覆盖)
        mLimitSendBytes = mOneSecondBytes * config.mAccumulateSeconds;

        mMaxSendSeconds = config.mMaxSendSeconds;
        mRedundancySeconds = config.mRedundancySeconds;
        mMaxBufferSeconds = config.mMaxBufferSeconds;

        SLOG_INFO << "AudioQWStreamProvider: create, audioId=" << mAudioId << ", accountId=" << mAccountId
                  << ", sampleRate=" << mFormatConfig.mSampleRate << ", channels=" << mFormatConfig.mChannels
                  << ", bitDepth=" << mFormatConfig.mBitDepth << ", oneSecondBytes=" << mOneSecondBytes
                  << ", limitSendBytes=" << mLimitSendBytes << ", maxBufferSeconds=" << mMaxBufferSeconds
                  << ", maxSendSeconds=" << mMaxSendSeconds << ", redundancySeconds=" << mRedundancySeconds
                  << ", isRealtime=" << mIsRealtime;

        mAudioBuffer.reserve(static_cast<size_t>(mMaxBufferSeconds) * mOneSecondBytes);

        LoadSpeakerDatabase();
        LoadHotwords();
    }

    AudioQWStreamProvider::~AudioQWStreamProvider() = default;

    size_t AudioQWStreamProvider::CalcOneSecondBytes() const {
        AudioUtilsConfig config;
        config.mSampleRate = mFormatConfig.mSampleRate;
        config.mChannels = mFormatConfig.mChannels;
        config.mBitDepth = mFormatConfig.mBitDepth;
        return AudioUtils::CalculateOneSecondBytes(config);
    }

    std::shared_ptr<qifeng::aas::ResultInfo> AudioQWStreamProvider::GetAudio(bool isNonblock) {
        SLOG_DEBUG << "AudioQWStreamProvider: get audio, isNonblock=" << isNonblock << ", audioId=" << mAudioId;
        return GetAudioRealtime(isNonblock);
    }

    std::shared_ptr<qifeng::aas::ResultInfo> AudioQWStreamProvider::GetAudioRealtime(bool isNonblock) {
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

        // sendStart紧接上次发送位置, 无冗余重叠, 保证数据唯一性
        size_t sendStart = mLastSentEnd;
        size_t sendEnd = sendStart + mLimitSendBytes;
        // 结束(停止)时冲刷剩余不足一个窗口的数据, 确保所有音频均经过QWen处理
        if (sendEnd > mWriteOffset) {
            sendEnd = mWriteOffset;
        }

        size_t startIdx = sendStart - mTrimOffset;
        size_t endIdx = sendEnd - mTrimOffset;
        auto result = BuildResultInfoFromBuffer(startIdx, endIdx, sendStart, sendEnd);
        mLastSentEnd = sendEnd;
        TrimBufferIfNeeded();

        return result;
    }

    void AudioQWStreamProvider::PushAudioData(const uint8_t* data, size_t len) {
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

    void AudioQWStreamProvider::Pause() {
        mPaused.store(true);
        mBufferCv.notify_all();
        SLOG_INFO << "AudioQWStreamProvider: paused, audioId=" << mAudioId;
    }

    void AudioQWStreamProvider::Resume() {
        mPaused.store(false);
        mBufferCv.notify_all();
        SLOG_INFO << "AudioQWStreamProvider: resumed, audioId=" << mAudioId;
    }

    void AudioQWStreamProvider::SignalEnd() {
        {
            std::lock_guard<std::mutex> lock(mBufferMutex);
            mEnded.store(true);
        }
        mBufferCv.notify_all();
        SLOG_INFO << "AudioQWStreamProvider: audio stream ended, audioId=" << mAudioId;
    }

    bool AudioQWStreamProvider::HasNewData() const {
        if (mLastSentEnd >= mWriteOffset) {
            return false;
        }
        size_t newBytes = mWriteOffset - mLastSentEnd;
        // 结束(停止)时冲刷剩余数据(大于1s), 确保所有音频均经过QWen处理
        if (mEnded.load()) {
            return newBytes > mOneSecondBytes;
        }
        return newBytes >= mLimitSendBytes;
    }

    bool AudioQWStreamProvider::HasPendingData() const {
        std::lock_guard<std::mutex> lock(mBufferMutex);
        return HasNewData();  // 结束时HasNewData会冲刷剩余数据, 不丢失
    }

    AudioQWStreamProvider::LostDataInfo AudioQWStreamProvider::GetLostDataInfo() const {
        LostDataInfo info;
        info.mTotalLostBytes = mTotalLostBytes.load(std::memory_order_relaxed);
        if (mOneSecondBytes > 0) {
            info.mTotalLostMs =
                static_cast<int64_t>(info.mTotalLostBytes) * 1000 / static_cast<int64_t>(mOneSecondBytes);
        }
        return info;
    }

    void AudioQWStreamProvider::TrimBufferIfNeeded() {
        size_t maxBufferBytes = static_cast<size_t>(mMaxBufferSeconds) * mOneSecondBytes;
        if (mAudioBuffer.size() <= maxBufferBytes) {
            return;
        }

        size_t trimSize = mAudioBuffer.size() - maxBufferBytes;
        size_t newTrimOffset = mTrimOffset + trimSize;

        // 统计未发送但被丢弃的数据量
        size_t lostBytes = 0;
        if (newTrimOffset > mLastSentEnd) {
            lostBytes = newTrimOffset - mLastSentEnd;
        }

        if (lostBytes > 0) {
            mTotalLostBytes.fetch_add(lostBytes, std::memory_order_relaxed);
            int64_t lostMs = 0;
            if (mOneSecondBytes > 0) {
                lostMs = static_cast<int64_t>(lostBytes) * 1000 / static_cast<int64_t>(mOneSecondBytes);
            }
            auto totalLost = mTotalLostBytes.load(std::memory_order_relaxed);
            SLOG_WARN << "AudioQWStreamProvider: buffer overflow, lost " << lostBytes << " bytes (" << lostMs
                      << "ms) of unsent audio, audioId=" << mAudioId << ", totalLost=" << totalLost << " bytes";
        }

        mAudioBuffer.erase(mAudioBuffer.begin(), mAudioBuffer.begin() + static_cast<ptrdiff_t>(trimSize));
        mTrimOffset = newTrimOffset;

        if (mLastSentEnd < mTrimOffset) {
            mLastSentEnd = mTrimOffset;
        }
    }

    std::shared_ptr<qifeng::aas::ResultInfo>
    AudioQWStreamProvider::BuildResultInfoFromBuffer(size_t startIdx, size_t endIdx, size_t sendStart, size_t sendEnd) {
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
    AudioQWStreamProvider::BuildResultInfoFromData(std::vector<uint8_t> &audioData, size_t sendStart, size_t sendEnd) {
        std::string capturedAudioId = mAudioId;
        uint64_t capturedAccountId = mAccountId;
        auto capturedCb = mTransResultCb;

        qifeng::aas::ResultInfo::SendCallBack callback = [this, capturedAudioId, capturedAccountId,
                                                          capturedCb](qifeng::aas::AasResult aasResult,
                                                                      qifeng::aas::BmsInfo bmsInfo) {
            SLOG_INFO << "AudioQWStreamProvider audioId=" << mAudioId << ", sentStart=" << mTestStartTime
                      << "ms, sentEnd=" << mTestEndTime << "ms, segments=" << aasResult.segments.size();

            // AAS内部存在历史数据缓存(约10s), 返回的startTime可能早于本次发送的startTime
            // 直接使用AAS返回的时间戳, 不做段重构或分句处理
            if (!aasResult.segments.empty()) {
                SLOG_DEBUG << "AudioQWStreamProvider: AAS result, firstSegStart="
                           << aasResult.segments.front().startTime
                           << "ms, lastSegEnd=" << aasResult.segments.back().endTime
                           << "ms, sentStart=" << mTestStartTime << "ms, audioId=" << capturedAudioId;
            }

            if (capturedCb) {
                capturedCb(capturedAudioId, capturedAccountId, aasResult);
                SLOG_INFO << "AudioQWStreamProvider: AAS callback done";
            } else {
                SLOG_ERROR << "AudioQWStreamProvider: AAS callback is NULL";
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
        bmsInfo.isOnline = true;

        SLOG_INFO << "BuildResultInfoFromData audioId=" << mAudioId << ", startTime=" << bmsInfo.mStartTime
                  << ", endTime=" << bmsInfo.mEndTime << ", dataSize=" << audioData.size();

        return std::make_shared<qifeng::aas::ResultInfo>(qifeng::aas::ResultInfo {
            callback, std::move(audioData), mFormatConfig, bmsInfo, mSvDataBase, mHotwords, mAudioId});
    }

    void AudioQWStreamProvider::LoadSpeakerDatabase() {
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

        SLOG_INFO << "AudioQWStreamProvider: loaded " << mSvDataBase->svDatabase.size()
                  << " speakers for accountId=" << mAccountId;
    }

    void AudioQWStreamProvider::LoadHotwords() {
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

        SLOG_INFO << "AudioQWStreamProvider: loaded " << mHotwords->hotwords.size()
                  << " hotwords for accountId=" << mAccountId;
    }

}  // namespace qifeng_ca
