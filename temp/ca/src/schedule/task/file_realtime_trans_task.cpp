//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <chrono>
#include <vector>

#include "qifeng_framework/aas/audio_load.h"
#include "qifeng_framework/common/logger.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"

#include "common/audio/audio_utils.h"
#include "common/audio_enums.h"
#include "common/timer_manager.h"
#include "internal/aas/audio_qw_transcribe_manager.h"
#include "internal/transcribe_handler.h"
#include "schedule/task/file_realtime_trans_task.h"

namespace qifeng_ca {

    FileRealtimeTransTask::FileRealtimeTransTask(const std::string &audioId, uint64_t accountId,
                                                 const std::string &srcFilePath, const std::string &taskFilePath,
                                                 bool restartTrans)
        : OfflineTransTask(audioId, accountId, srcFilePath, taskFilePath, restartTrans) {
        // 实时模拟: 每次2s分片(基类默认30s), 复用基类ParseWavHeader据此计算mChunkSize
        mTotalTime = ChunkSeconds;
    }

    FileRealtimeTransTask::~FileRealtimeTransTask() {
        CancelTimer();
        if (mRunning.exchange(false)) {
            EndAudioStream();
        }
    }

    bool FileRealtimeTransTask::StartAasTranscribe() {
        // 实时provider: isRealtime=true, 2s推送窗口, 复用基类解析的mWavInfo格式
        AudioQWStreamProvider::Config config;
        config.mAccumulateSeconds = ChunkSeconds;  // 保持2s实时模拟分片(默认30s)
        config.mFormatConfig.mSampleRate = mWavInfo.mSampleRate;
        config.mFormatConfig.mChannels = mWavInfo.mChannels;
        config.mFormatConfig.mBitDepth = mWavInfo.mBitDepth;
        config.mFormatConfigSet = true;

        auto provider = std::make_shared<AudioQWStreamProvider>(mAudioId, mAccountId, true, config);
        mHandler = std::make_shared<TranscribeHandler>(mAudioId, mAccountId, true);
        AttachTransResultCallback(provider.get());
        mProvider = provider;

        // 打开任务文件供读循环读取(跳过WAV头)
        mSharedFile = std::make_shared<std::ifstream>(mTaskFilePath, std::ios::binary);
        if (!mSharedFile->is_open()) {
            SLOG_ERROR << "FileRealtimeTransTask: open task file failed, path=" << mTaskFilePath;
            mProvider.reset();
            mHandler.reset();
            return false;
        }
        mSharedFile->seekg(AudioUtils::WavHeaderSize, std::ios::beg);

        mTimerName = "FileRtTrans_" + mAudioId;
        TimerManager::GetInstance().Register(mTimerName);

        qifeng::aas::AasQwenJobStart(std::static_pointer_cast<qifeng::aas::GetAudioBase>(mProvider));
        SLOG_INFO << "FileRealtimeTransTask: start AAS (realtime provider, 2s chunks), audioId=" << mAudioId;
        ScheduleNext();
        return true;
    }

    void FileRealtimeTransTask::DoReadCycle() {
        if (!mRunning.load() || !mSharedFile || !mSharedFile->is_open()) {
            SLOG_INFO << "FileRealtimeTransTask: read cycle stopped, audioId=" << mAudioId;
            return;
        }

        std::vector<uint8_t> buffer(mChunkSize);
        mSharedFile->read(reinterpret_cast<char*>(buffer.data()),  // NOLINT
                          static_cast<std::streamsize>(mChunkSize));
        auto bytesRead = static_cast<size_t>(mSharedFile->gcount());
        SLOG_INFO << "FileRealtimeTransTask: read " << bytesRead << " bytes, audioId=" << mAudioId;

        if (bytesRead == 0) {
            // EOF: 自然结束(drain)
            OnTransComplete();
            return;
        }

        if (mProvider) {
            mProvider->PushAudioData(buffer.data(), bytesRead);
        }
        ScheduleNext();
    }

    void FileRealtimeTransTask::ScheduleNext() {
        if (!mRunning.load()) {
            SLOG_INFO << "FileRealtimeTransTask: schedule next read cycle stopped, audioId=" << mAudioId;
            return;
        }
        auto self = Self();
        uint32_t generation = mRunGeneration.load(std::memory_order_relaxed);
        auto* timerTask =
            WFTaskFactory::create_timer_task(mTimerName, ReadIntervalSec, 0, [self, generation](WFTimerTask* task) {
                SLOG_INFO << "FileRealtimeTransTask: timer callback, audioId=" << self->mAudioId;
                if (task->get_state()) {
                    SLOG_ERROR << "FileRealtimeTransTask: timer callback, state: " << task->get_state()
                               << ", error: " << task->get_error() << ", audioId=" << self->mAudioId;
                    return;
                }
                // 代次不匹配说明此回调来自上一次被抢占的任务, 直接退出
                if (self->mRunGeneration.load(std::memory_order_relaxed) != generation) {
                    SLOG_INFO << "FileRealtimeTransTask: stale timer detected, skip, audioId=" << self->mAudioId
                              << " gen=" << generation;
                    return;
                }
                self->DoReadCycle();
            });
        timerTask->start();
    }

    void FileRealtimeTransTask::OnTransComplete() {
        if (!mRunning.exchange(false)) {
            return;
        }
        EndAudioStreamWithDrain();
        SetComplete();
        SLOG_INFO << "FileRealtimeTransTask: transcribe complete, audioId=" << mAudioId;
    }

    void FileRealtimeTransTask::EndAudioStream() {
        if (!mProvider) {
            SLOG_INFO << "FileRealtimeTransTask: provider is null, audioId=" << mAudioId;
            return;
        }
        if (mHandler) {
            mHandler->NotifyFinal();
        }
        mProvider->SignalEnd();
        qifeng::aas::AasJobStop();
        mProvider.reset();
        mHandler.reset();
        SLOG_INFO << "FileRealtimeTransTask: end audio stream, audioId=" << mAudioId;
    }

    void FileRealtimeTransTask::EndAudioStreamWithDrain() {
        if (!mProvider) {
            return;
        }
        if (!mProvider->IsEnded()) {
            mProvider->SignalEnd();
        }
        // 等待AAS消费完缓冲区剩余数据, 避免尾部转写丢失
        int waitedMs = 0;
        while (mProvider->HasPendingData() && waitedMs < DrainMaxWaitMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(DrainCheckStepMs));
            waitedMs += DrainCheckStepMs;
        }
        if (waitedMs >= DrainMaxWaitMs) {
            SLOG_WARN << "FileRealtimeTransTask: drain timeout, audioId=" << mAudioId
                      << " hasPendingData=" << mProvider->HasPendingData();
        }
        if (mHandler) {
            mHandler->NotifyFinal();
        }
        qifeng::aas::AasJobStop();
        mProvider.reset();
        mHandler.reset();
        SLOG_INFO << "FileRealtimeTransTask: end audio stream with drain, audioId=" << mAudioId;
    }

    void FileRealtimeTransTask::CancelTimer() {
        if (!mTimerName.empty()) {
            TimerManager::GetInstance().CancelByName(mTimerName);
        }
    }

    void FileRealtimeTransTask::EstimateAndPersistPlanFinishTime() {
        // 文件模拟实时转写不做预估完成时间(与实时录音一致)
    }

    std::string_view FileRealtimeTransTask::GetTranscribingMsg() const {
        return AudioStatusMsg::FileRealtimeTranscribing;
    }

    std::string_view FileRealtimeTransTask::GetTransFailedMsg() const {
        return AudioStatusMsg::FileRealtimeTransFailed;
    }

}  // namespace qifeng_ca
