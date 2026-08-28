//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_AAS_AUDIO_QW_TRANSCRIBE_MANAGER_H
#define QIFENG_CA_INCLUDE_INTERNAL_AAS_AUDIO_QW_TRANSCRIBE_MANAGER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "qifeng_framework/aas/aas_callback.h"

#include "internal/aas/bms_audio_provider.h"

namespace qifeng_ca {

    class TranscribeHandler;

    class AudioQWStreamProvider : public BmsAudioProvider {
    public:
        struct Config {
            int mMaxSendSeconds;                      // 最大的发窗口
            int mRedundancySeconds;                   // 最大数据冗余窗口
            int mMaxBufferSeconds;                    // 最大的缓存区大小
            int mAccumulateSeconds {30};              // QWen累计发送窗口(秒): 累计该时长后调用QWen接口
            qifeng::aas::FormatConfig mFormatConfig;  // 音频格式配置(可选)
            bool mFormatConfigSet;                    // 是否设置了自定义格式配置

            Config() : mMaxSendSeconds(60), mRedundancySeconds(59), mMaxBufferSeconds(120), mFormatConfigSet(false) {}
        };

        // 数据丢失统计信息
        struct LostDataInfo {
            size_t mTotalLostBytes {0};  // 累计丢失字节数
            int64_t mTotalLostMs {0};    // 累计丢失时长(毫秒)
        };

        AudioQWStreamProvider(const std::string &audioId, uint64_t accountId, bool isRealtime,
                              const Config &config = {});

        ~AudioQWStreamProvider() override;

        AudioQWStreamProvider(const AudioQWStreamProvider &) = delete;
        AudioQWStreamProvider(AudioQWStreamProvider &&) = delete;
        AudioQWStreamProvider &operator=(const AudioQWStreamProvider &) = delete;
        AudioQWStreamProvider &operator=(AudioQWStreamProvider &&) = delete;

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

        // 获取累计数据丢失统计
        LostDataInfo GetLostDataInfo() const;

    private:
        size_t CalcOneSecondBytes() const;
        void LoadSpeakerDatabase();
        void LoadHotwords();

        std::shared_ptr<qifeng::aas::ResultInfo> GetAudioRealtime(bool isNonblock);

        bool HasNewData() const;
        void TrimBufferIfNeeded();
        std::shared_ptr<qifeng::aas::ResultInfo> BuildResultInfoFromBuffer(size_t startIdx, size_t endIdx,
                                                                           size_t sendStart, size_t sendEnd);
        std::shared_ptr<qifeng::aas::ResultInfo> BuildResultInfoFromData(std::vector<uint8_t> &audioData,
                                                                         size_t sendStart, size_t sendEnd);

    private:
        bool mIsRealtime {true};

        mutable std::mutex mBufferMutex;
        std::condition_variable mBufferCv;
        std::atomic<bool> mEnded {false};
        std::atomic<bool> mPaused {false};

        std::vector<uint8_t> mAudioBuffer;
        size_t mTrimOffset {0};
        size_t mWriteOffset {0};  // 当前数据写入数据的位置
        size_t mLastSentEnd {0};  // 上次发送后的数据位置(去重基准)

        size_t mOneSecondBytes {0};
        size_t mLimitSendBytes {0};  // 每次固定发送2s数据

        // 缓冲区、冗余、发送窗口
        int mMaxSendSeconds {60};
        int mRedundancySeconds {59};
        int mMaxBufferSeconds {120};

        qifeng::aas::FormatConfig mFormatConfig;

        std::shared_ptr<qifeng::aas::SvDataBase> mSvDataBase;
        std::shared_ptr<qifeng::aas::HotWords> mHotwords;

        // 数据丢失统计: 缓冲区溢出导致未发送数据被丢弃的累计字节数
        std::atomic<size_t> mTotalLostBytes {0};

        int64_t mTestStartTime {0};  // 最近一次发送的起始时间(ms)
        int64_t mTestEndTime {0};    // 最近一次发送的结束时间(ms)
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_AAS_AUDIO_QW_TRANSCRIBE_MANAGER_H
