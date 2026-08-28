//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_AAS_OFFLINE_QW_STREAM_PROVIDER_H
#define QIFENG_CA_INCLUDE_INTERNAL_AAS_OFFLINE_QW_STREAM_PROVIDER_H

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

    // 离线转写专用 Provider (QW版): 固定10s分片
    class OfflineQWStreamProvider : public BmsAudioProvider {
    public:
        struct Config {
            int mMaxBufferSeconds;                    // 最大的缓存区大小(秒)
            qifeng::aas::FormatConfig mFormatConfig;  // 音频格式配置(可选)
            bool mFormatConfigSet;                    // 是否设置了自定义格式配置

            Config() : mMaxBufferSeconds(120), mFormatConfigSet(false) {}
        };

        OfflineQWStreamProvider(const std::string &audioId, uint64_t accountId, const Config &config = {});

        ~OfflineQWStreamProvider() override;

        OfflineQWStreamProvider(const OfflineQWStreamProvider &) = delete;
        OfflineQWStreamProvider(OfflineQWStreamProvider &&) = delete;
        OfflineQWStreamProvider &operator=(const OfflineQWStreamProvider &) = delete;
        OfflineQWStreamProvider &operator=(OfflineQWStreamProvider &&) = delete;

        // GetAudioBase 接口
        std::shared_ptr<qifeng::aas::ResultInfo> GetAudio(bool isNonblock) override;

        // BmsAudioProvider 接口
        void SignalEnd() override;
        bool IsEnded() const override { return mEnded.load(); }
        bool IsRealtime() const override { return false; }
        void PushAudioData(const uint8_t* data, size_t len) override;
        void Pause() override { /* 离线模式不支持暂停 */ }
        void Resume() override { /* 离线模式不支持恢复 */ }
        bool HasPendingData() const override;

        // 设置离线数据准备回调
        void SetDataPreparer(DataPreparer preparer) override;

    private:
        size_t CalcOneSecondBytes() const;
        void LoadSpeakerDatabase();
        void LoadHotwords();

        std::shared_ptr<qifeng::aas::ResultInfo> GetAudioOffline();

        bool HasNewData() const;
        void TrimBufferIfNeeded();
        void FeedOfflineDataToBuffer();
        std::shared_ptr<qifeng::aas::ResultInfo> BuildResultInfoFromBuffer(size_t startIdx, size_t endIdx,
                                                                           size_t sendStart, size_t sendEnd);
        std::shared_ptr<qifeng::aas::ResultInfo> BuildResultInfoFromData(std::vector<uint8_t> &audioData,
                                                                         size_t sendStart, size_t sendEnd);

    private:
        mutable std::mutex mBufferMutex;
        std::condition_variable mBufferCv;
        std::atomic<bool> mEnded {false};

        std::vector<uint8_t> mAudioBuffer;
        size_t mTrimOffset {0};
        size_t mWriteOffset {0};
        size_t mLastSentEnd {0};

        size_t mOneSecondBytes {0};
        size_t mChunkSendBytes {0};  // 固定10s分片字节数

        int mMaxBufferSeconds {120};

        qifeng::aas::FormatConfig mFormatConfig;

        std::shared_ptr<qifeng::aas::SvDataBase> mSvDataBase;
        std::shared_ptr<qifeng::aas::HotWords> mHotwords;

        // 离线数据准备回调
        DataPreparer mDataPreparer;
        std::atomic<bool> mDataPreparerEof {false};

        // 分片序号(用于传输连续性检测)
        uint64_t mChunkSequence {0};

        int64_t mTestStartTime {0};
        int64_t mTestEndTime {0};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_AAS_OFFLINE_QW_STREAM_PROVIDER_H
