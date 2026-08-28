//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_AAS_AUDIO_TRANSCRIBE_MANAGER_H
#define QIFENG_CA_INCLUDE_INTERNAL_AAS_AUDIO_TRANSCRIBE_MANAGER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "aas/aas_callback.h"
#include "internal/aas/bms_audio_provider.h"

namespace qifeng_ca {

    class TranscribeHandler;

    class AudioStreamProvider : public BmsAudioProvider {
    public:
        struct Config {
            int mMaxSendSeconds;                      // 最大的发窗口
            int mRedundancySeconds;                   // 最大数据冗余窗口
            int mMaxBufferSeconds;                    // 最大的缓存区大小
            qifeng::aas::FormatConfig mFormatConfig;  // 音频格式配置(可选)
            bool mFormatConfigSet;                    // 是否设置了自定义格式配置

            Config() : mMaxSendSeconds(60), mRedundancySeconds(59), mMaxBufferSeconds(120), mFormatConfigSet(false) {}
        };

        AudioStreamProvider(const std::string &audioId, uint64_t accountId, bool isRealtime, const Config &config = {});

        ~AudioStreamProvider() override;

        AudioStreamProvider(const AudioStreamProvider &) = delete;
        AudioStreamProvider(AudioStreamProvider &&) = delete;
        AudioStreamProvider &operator=(const AudioStreamProvider &) = delete;
        AudioStreamProvider &operator=(AudioStreamProvider &&) = delete;

        std::shared_ptr<qifeng::aas::ResultInfo> GetAudio(bool isNonblock) override;

        void SignalEnd() override;
        bool IsEnded() const override { return mEnded.load(); }
        bool IsRealtime() const override { return mIsRealtime; }

        void PushAudioData(const uint8_t* data, size_t len) override;

        // 暂停/继续: 暂停时GetAudio阻塞不处理数据, 音频时长自然暂停
        void Pause() override;
        void Resume() override;

        // 检查缓冲区是否还有未处理的数据(对外提供)
        bool HasPendingData() const override;

        // 离线数据准备回调: 离线模式下由外部提供数据读取逻辑
        // 回调返回true表示还有数据, false表示数据已读完
        using DataPreparer = std::function<bool(std::vector<uint8_t> &)>;
        void SetDataPreparer(DataPreparer preparer);

    private:
        size_t CalcOneSecondBytes() const;
        void LoadSpeakerDatabase();
        void LoadHotwords();

        // 重构AasResult: 合并完全连续的段(isFullSegment=false且时间首尾相接), 并对超过
        // redundancySeconds时长的段强制标记 isFullSegment=true
        static qifeng::aas::AasResult RebuildSegments(qifeng::aas::AasResult src, int redundancySeconds,
                                                      const std::string audioId);

        std::shared_ptr<qifeng::aas::ResultInfo> GetAudioRealtime(bool isNonblock);
        std::shared_ptr<qifeng::aas::ResultInfo> GetAudioOffline();

        bool HasNewData() const;
        size_t CalcSendStartOffsetFor(size_t sendEnd) const;
        void TrimBufferIfNeeded();
        std::shared_ptr<qifeng::aas::ResultInfo> BuildResultInfoFromBuffer(size_t startIdx, size_t endIdx,
                                                                           size_t sendStart, size_t sendEnd);
        std::shared_ptr<qifeng::aas::ResultInfo> BuildResultInfoFromData(std::vector<uint8_t> &audioData,
                                                                         size_t sendStart, size_t sendEnd);

        void FeedOfflineDataToBuffer();

    private:
        bool mIsRealtime {true};

        mutable std::mutex mBufferMutex;
        std::condition_variable mBufferCv;
        std::atomic<bool> mEnded {false};
        std::atomic<bool> mPaused {false};

        std::vector<uint8_t> mAudioBuffer;
        size_t mTrimOffset {0};
        size_t mWriteOffset {0};  // 当前数据写入数据的位置
        size_t mLastSentEnd {0};  // 上次发送后的数据位置

        size_t mOneSecondBytes {0};

        // 缓冲区、冗余、发送窗口
        int mMaxSendSeconds {60};
        int mRedundancySeconds {59};
        int mMaxBufferSeconds {120};

        qifeng::aas::FormatConfig mFormatConfig;

        std::shared_ptr<qifeng::aas::SvDataBase> mSvDataBase;
        std::shared_ptr<qifeng::aas::HotWords> mHotwords;

        // 离线数据准备回调
        DataPreparer mDataPreparer;
        std::atomic<bool> mDataPreparerEof {false};

        // 分片检测: 最后一个分片结束的字节偏移, 下次发送从此位置开始裁剪冗余
        std::atomic<size_t> mFragmentCutOffset {0};

        size_t mTestStartTime = 0;
        size_t mTestEndTime = 0;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_AAS_AUDIO_TRANSCRIBE_MANAGER_H
