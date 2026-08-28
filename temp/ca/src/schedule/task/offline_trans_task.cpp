//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/audio/audio_utils.h"
#include "common/audio_enums.h"
#include "common/config/schedule_config.h"
#include "dao/trans_dao.h"
#include "dao_managers/meeting_dao_manager.h"
#include "internal/aas/offline_qw_stream_provider.h"
#include "internal/transcribe_handler.h"
#include "schedule/pcm/pcm_engine.h"
#include "schedule/task/offline_trans_task.h"
#include "schedule/task/summary_task.h"
#include "schedule/task/task_db_helper.h"

namespace qifeng_ca {

    OfflineTransTask::OfflineTransTask(const std::string &audioId, uint64_t accountId, const std::string &srcFilePath,
                                       const std::string &taskFilePath, bool restartTrans)
        : BaseTask(audioId, accountId), mSrcFilePath(srcFilePath), mTaskFilePath(taskFilePath),
          mRestartTrans(restartTrans) {
        mAasFormatConfig.mSampleRate = 16000;
        mAasFormatConfig.mBitDepth = 16;
        mAasFormatConfig.mChannels = 1;
        mTotalTime = 30;

        mUseQwProvider = 1;
    }

    OfflineTransTask::~OfflineTransTask() {
        OnTransComplete();
    }

    void OfflineTransTask::Start() {
        SetRunning(true);
        // 递增运行代次, 使上一次被抢占的 DataPreparer 回调变为过期状态
        mRunGeneration.fetch_add(1, std::memory_order_relaxed);
        ScopeExit complete([this]() {
            SLOG_INFO << "OfflineTransTask: complete, audioId=" << mAudioId;
            SetComplete();
        });
        if (!PrepareTaskFile()) {
            SLOG_ERROR << "OfflineTransTask: prepare task file failed, audioId=" << mAudioId;
            TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::TransException,
                                            AudioStatusMsg::AudioFileMissing);
            return;
        }
        if (!ParseWavHeader()) {
            TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::TransException,
                                            AudioStatusMsg::AudioFileParseFailed);
            return;
        }
        // 重启转写: 清理旧转写记录
        if (mRestartTrans) {
            SLOG_INFO << "OfflineTransTask: restart trans, audioId=" << mAudioId;
            TransDao transDao;
            transDao.DeleteByAudioId(mAccountId, mAudioId);
        }
        if (!StartAasTranscribe()) {
            SLOG_ERROR << "OfflineTransTask: start AAS offline trans failed, audioId=" << mAudioId;
            TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::TransException, GetTransFailedMsg());
            return;
        }
        complete.Release();
        mTransStarted = true;
        mTransStartTime = GetTimeMs();
        auto &daoMg = MeetingDaoManager::GetInstance();
        daoMg.UpdateTransStartTime(mAccountId, mAudioId, static_cast<int64_t>(mTransStartTime));
        EstimateAndPersistPlanFinishTime();
        TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::Transing, GetTranscribingMsg());
        SLOG_INFO << "OfflineTransTask: started, audioId=" << mAudioId << " taskFile=" << mTaskFilePath;
    }

    bool OfflineTransTask::ParseWavHeader() {
        bool ok = AudioUtils::ParseWavHeader(mSrcFilePath, mWavInfo);
        if (!ok) {
            SLOG_ERROR << "OfflineTransTask: parse wav header failed, path=" << mSrcFilePath;
            return false;
        }
        AudioUtilsConfig wavCfg {static_cast<uint32_t>(mWavInfo.mSampleRate), static_cast<uint16_t>(mWavInfo.mChannels),
                                 static_cast<uint16_t>(mWavInfo.mBitDepth)};
        size_t oneSecBytes = AudioUtils::CalculateOneSecondBytes(wavCfg);
        mChunkSize = oneSecBytes * mTotalTime;
        SLOG_INFO << "OfflineTransTask: chunkSize=" << mChunkSize << ", sampleRate=" << mWavInfo.mSampleRate
                  << ", channels=" << mWavInfo.mChannels << ", bitDepth=" << mWavInfo.mBitDepth
                  << ", total size=" << mWavInfo.mDataSize;
        return true;
    }

    void OfflineTransTask::Preempt() {
        SLOG_INFO << "OfflineTransTask: preempt, audioId=" << mAudioId;
        if (!mRunning.exchange(false)) {
            return;
        }
        SLOG_INFO << "OfflineTransTask: stopping, audioId=" << mAudioId;
        mTransBytesRead = 0;
        EndAudioStream();
        TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::WaitTrans, AudioStatusMsg::WaitTrans);
        auto &daoMg = MeetingDaoManager::GetInstance();
        daoMg.UpdatePlanFinishTime(mAccountId, mAudioId, 0);  // 重置时间
        daoMg.UpdateTransProgress(mAccountId, mAudioId, 0);   // 重置进度
        daoMg.UpdateTransStartTime(mAccountId, mAudioId, 0);  // 重置开始时间
    }

    void OfflineTransTask::Stop() {
        if (!mRunning.exchange(false)) {
            return;
        }
        SLOG_INFO << "OfflineTransTask: stopping, audioId=" << mAudioId;
        EndAudioStream();
        SetComplete();
    }

    void OfflineTransTask::Cancel() {
        // 区分"取消"与"正常停止": 置 mCancelled, After 据此跳过提交 SummaryTask,
        // 防止删除转写中录音后仍触发总结任务
        mCancelled.store(true, std::memory_order_release);
        Stop();
    }

    bool OfflineTransTask::PrepareTaskFile() {
        if (mSrcFilePath.empty() || !std::filesystem::exists(mSrcFilePath)) {
            SLOG_ERROR << "OfflineTransTask: source file not exist, path=" << mSrcFilePath;
            return false;
        }
        // 校验DataPreparer读取的task文件路径(可能与srcFilePath不同)
        if (mTaskFilePath.empty() || !std::filesystem::exists(mTaskFilePath)) {
            SLOG_ERROR << "OfflineTransTask: task file not exist, path=" << mTaskFilePath;
            return false;
        }
        return true;
    }

    void OfflineTransTask::AttachTransResultCallback(BmsAudioProvider* provider) {
        auto handler = mHandler;
        provider->SetTransResultCallback(
            [handler](const std::string & /*aid*/, uint64_t /*accId*/, const qifeng::aas::AasResult &result) {
                handler->OnAasResult(result);
            });
    }

    bool OfflineTransTask::StartAasTranscribe() {
        auto preparer = BuildDataPreparer();
        // BuildDataPreparer内部可能因文件打开失败触发OnTransFailed, 此时直接返回
        if (!mRunning.load()) {
            return false;
        }

        mHandler = std::make_shared<TranscribeHandler>(mAudioId, mAccountId, false);

        if (mUseQwProvider) {
            // OfflineQWStreamProvider: 固定10s无冗余分片, 带序号校验
            OfflineQWStreamProvider::Config config;
            config.mMaxBufferSeconds = 120;
            config.mFormatConfig.mSampleRate = mWavInfo.mSampleRate;
            config.mFormatConfig.mChannels = mWavInfo.mChannels;
            config.mFormatConfig.mBitDepth = mWavInfo.mBitDepth;
            config.mFormatConfigSet = true;

            auto provider = std::make_shared<OfflineQWStreamProvider>(mAudioId, mAccountId, config);
            provider->SetDataPreparer(preparer);
            AttachTransResultCallback(provider.get());
            mProvider = provider;
            SLOG_INFO << "OfflineTransTask: start AAS (QW provider, 10s chunks), audioId=" << mAudioId;
            qifeng::aas::AasQwenJobStart(std::static_pointer_cast<qifeng::aas::GetAudioBase>(provider));
        } else {
            // AudioStreamProvider: 有冗余窗口, 默认90s发送窗口
            AudioStreamProvider::Config config;
            config.mMaxSendSeconds = 90;
            config.mRedundancySeconds = 59;
            config.mMaxBufferSeconds = 120;
            config.mFormatConfig.mSampleRate = mWavInfo.mSampleRate;
            config.mFormatConfig.mChannels = mWavInfo.mChannels;
            config.mFormatConfig.mBitDepth = mWavInfo.mBitDepth;
            config.mFormatConfigSet = true;

            auto provider = std::make_shared<AudioStreamProvider>(mAudioId, mAccountId, false, config);
            provider->SetDataPreparer(preparer);
            AttachTransResultCallback(provider.get());
            mProvider = provider;
            SLOG_INFO << "OfflineTransTask: start AAS (legacy provider), audioId=" << mAudioId;
            qifeng::aas::AasParaformerJobStart(std::static_pointer_cast<qifeng::aas::GetAudioBase>(provider));
        }

        return true;
    }

    void OfflineTransTask::EndAudioStream() {
        if (!mProvider) {
            return;
        }
        mProvider->SignalEnd();
        qifeng::aas::AasJobStop();
        mProvider.reset();
        mHandler.reset();
        SLOG_INFO << "OfflineTransTask: end audio stream, audioId=" << mAudioId;
    }

    AudioStreamProvider::DataPreparer OfflineTransTask::BuildDataPreparer() {
        // 捕获taskFilePath的副本, 确保生命周期安全
        std::string filePath = mTaskFilePath;

        // 创建共享的ifstream, 在DataPreparer多次调用间保持文件读取位置
        auto sharedFile = std::make_shared<std::ifstream>(filePath, std::ios::binary);
        if (!sharedFile->is_open()) {
            SLOG_ERROR << "OfflineTransTask: cannot open task file for DataPreparer, path=" << filePath;
            // 文件打开失败: 通过OnTransFailed设置错误状态并终止任务, 避免无限卡死
            OnTransFailed(AudioStatusMsg::AudioFileMissing);
            return [](std::vector<uint8_t> &) { return false; };
        }

        // 跳过WAV头
        sharedFile->seekg(AudioUtils::WavHeaderSize, std::ios::beg);
        auto self = Self();
        // 捕获当前运行代次, 用于过滤被抢占后残留的旧 DataPreparer 回调
        uint32_t generation = mRunGeneration.load(std::memory_order_relaxed);
        SLOG_DEBUG << "OfflineTransTask: build data preparer, audioId=" << mAudioId << " gen=" << generation;

        return [sharedFile, self, generation](std::vector<uint8_t> &outData) -> bool {
            return self->ReadNextChunk(sharedFile, outData, generation);
        };
    }

    // 流式读取下一块音频数据, 供 DataPreparer 回调调用
    bool OfflineTransTask::ReadNextChunk(const std::shared_ptr<std::ifstream> &sharedFile,
                                         std::vector<uint8_t> &outData, uint32_t generation) {
        // 代次不匹配说明此回调来自上一次被抢占的 AAS job, 直接退出不触发任何副作用
        if (mRunGeneration.load(std::memory_order_relaxed) != generation) {
            SLOG_INFO << "OfflineTransTask: stale data preparer detected, skip, audioId=" << mAudioId
                      << " gen=" << generation;
            return false;
        }
        SLOG_DEBUG << "OfflineTransTask: read data, audioId=" << mAudioId;
        if (!sharedFile || !sharedFile->is_open()) {
            return false;
        }

        // 超时检查: 超过40分钟则标记超时错误并终止
        CheckTransTimeout();
        if (!mRunning.load()) {
            return false;
        }

        outData.resize(mChunkSize);
        sharedFile->read(reinterpret_cast<char*>(outData.data()),  // NOLINT
                         static_cast<std::streamsize>(mChunkSize));
        auto bytesRead = static_cast<size_t>(sharedFile->gcount());

        SLOG_DEBUG << "OfflineTransTask: read " << bytesRead << " bytes, audioId=" << mAudioId;
        if (bytesRead == 0) {
            sharedFile->close();
            // 无数据、停止离线任务
            OnTransComplete();
            return false;
        }

        outData.resize(bytesRead);
        UpdateTransProgressByBytes(bytesRead);
        RefreshPlanFinishTimeIfNeeded();
        return true;
    }

    void OfflineTransTask::OnTransComplete() {
        if (!mRunning.exchange(false)) {
            return;
        }
        EndAudioStream();
        PersistTransDuration();
        SetComplete();
        SLOG_INFO << "OfflineTransTask: transcribe complete, audioId=" << mAudioId;
    }

    void OfflineTransTask::OnTransFailed(std::string_view message) {
        mFailed = true;
        SLOG_ERROR << "OfflineTransTask: transcribe failed, audioId=" << mAudioId << " msg=" << message;

        TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::TransException, message);
        OnTransComplete();
    }

    void OfflineTransTask::CheckTransTimeout() {
        // 仅在转写启动后检查
        if (!mTransStarted || mTransStartTime == 0) {
            return;
        }
        uint64_t transTimeoutMs = ScheduleConfig::GetInstance().GetOfflineTransTimeoutMs();
        uint64_t elapsedMs = GetTimeMs() - mTransStartTime;
        if (elapsedMs > transTimeoutMs) {
            SLOG_ERROR << "OfflineTransTask: transcribe timeout, audioId=" << mAudioId << " elapsedMs=" << elapsedMs
                       << " thresholdMs=" << transTimeoutMs;
            OnTransFailed(AudioStatusMsg::OfflineTransTimeout);
        }
    }

    void OfflineTransTask::PersistTransDuration() {
        // 仅在转写真正启动后才统计耗时
        if (!mTransStarted) {
            return;
        }
        auto durationMs = GetTimeMs() - mTransStartTime;
        auto &daoMg = MeetingDaoManager::GetInstance();
        daoMg.UpdateTransDuration(mAccountId, mAudioId, static_cast<int64_t>(durationMs));
        SLOG_INFO << "OfflineTransTask: trans duration=" << durationMs << "ms, audioId=" << mAudioId;
    }

    void OfflineTransTask::EstimateAndPersistPlanFinishTime() {
        // 每次DataPreparer回调约10s处理mChunkSize字节(60秒音频)
        if (mChunkSize == 0 || mWavInfo.mDataSize == 0) {
            return;
        }
        // 总回调次数 = 总字节数 / 每次处理字节数
        uint64_t totalChunks = (mWavInfo.mDataSize + mChunkSize - 1) / mChunkSize;
        // 预估总耗时: 暂时默认aas每次回调约10s
        constexpr uint64_t msPerChunk = static_cast<uint64_t>(5.5 * 1000);
        constexpr uint64_t oneMinMs = 60LL * 1000;
        uint64_t estimatedMs = totalChunks * msPerChunk + oneMinMs;  // 默认加上1分钟，防止显示屏显示0分钟
        int64_t planFinish = static_cast<int64_t>(mTransStartTime + estimatedMs);
        mInitialPlanFinishTime = planFinish;
        auto &daoMg = MeetingDaoManager::GetInstance();
        daoMg.UpdatePlanFinishTime(mAccountId, mAudioId, planFinish);
        SLOG_INFO << "OfflineTransTask: estimate planFinish=" << planFinish << " totalChunks=" << totalChunks
                  << " audioId=" << mAudioId;
    }

    void OfflineTransTask::RefreshPlanFinishTimeIfNeeded() {
        if (mInitialPlanFinishTime == 0 || mTransBytesRead == 0 || mWavInfo.mDataSize == 0) {
            return;
        }
        // 仅当实际耗时接近初始预估时(达到90%)才重新计算
        int64_t now = static_cast<int64_t>(GetTimeMs());
        int64_t startMs = static_cast<int64_t>(mTransStartTime);
        int64_t totalEstimateMs = mInitialPlanFinishTime - startMs;
        int64_t elapsedMs = now - startMs;
        if (elapsedMs < totalEstimateMs * 9 / 10) {
            return;
        }
        // 根据当前进度重新计算: 已用时间 / 进度比例 = 总预估时间
        double progress = static_cast<double>(mTransBytesRead) / static_cast<double>(mWavInfo.mDataSize);
        if (progress <= 0.0 || progress >= 1.0) {
            return;
        }
        int64_t newEstimateMs = static_cast<int64_t>(static_cast<double>(elapsedMs) / progress);
        int64_t newPlanFinish = startMs + newEstimateMs;
        if (newPlanFinish <= now) {
            return;
        }
        auto &daoMg = MeetingDaoManager::GetInstance();
        daoMg.UpdatePlanFinishTime(mAccountId, mAudioId, newPlanFinish);
        mInitialPlanFinishTime = newPlanFinish;
        SLOG_INFO << "OfflineTransTask: refresh planFinish=" << newPlanFinish << " progress=" << progress
                  << " elapsedMs=" << elapsedMs << " audioId=" << mAudioId;
    }

    void OfflineTransTask::UpdateTransProgressByBytes(size_t bytesRead) {
        mTransBytesRead += bytesRead;
        if (mWavInfo.mDataSize == 0) {
            return;
        }
        int progress = static_cast<int>(mTransBytesRead * 100 / static_cast<size_t>(mWavInfo.mDataSize));
        if (progress > 100) {
            progress = 100;
        }
        auto &daoMg = MeetingDaoManager::GetInstance();
        daoMg.UpdateTransProgress(mAccountId, mAudioId, progress);
    }

    void OfflineTransTask::After() {
        // 被取消(删除录音触发): 不提交总结任务, 也不改 DB 状态
        if (mCancelled.load(std::memory_order_acquire)) {
            SLOG_INFO << "OfflineTransTask: cancelled, skip After, audioId=" << mAudioId;
            return;
        }
        // 仅在转写真正启动后才提交总结任务(失败时不应触发)
        if (!mTransStarted || mFailed) {
            return;
        }
        TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::WaitSummary, AudioStatusMsg::WaitSummary);
        auto summaryTask = std::make_shared<SummaryTask>(mAudioId, mAccountId);
        PcmEngine::GetInstance().Submit(summaryTask);
        SLOG_INFO << "OfflineTransTask: summary task submitted, audioId=" << mAudioId;
    }

}  // namespace qifeng_ca
