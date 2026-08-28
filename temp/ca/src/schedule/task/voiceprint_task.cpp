//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <aas/aas_callback.h>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <vector>

#include "qifeng_framework/aas/audio_load.h"
#include "qifeng_framework/common/logger.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"

#include "common/audio/audio_utils.h"
#include "common/config/hal_config.h"
#include "common/timer_manager.h"
#include "common/utils/file_name_generator.h"
#include "common/utils/file_opt.h"
#include "internal/hal/hal_bridge.h"
#include "schedule/task/voiceprint_task.h"

namespace qifeng_ca {

    VoiceprintTask::VoiceprintTask(const std::string &audioId, uint64_t accountId) : BaseTask(audioId, accountId) {
        auto &cfg = HalConfig::GetInstance();
        mRecordSampleRate = static_cast<int>(cfg.GetRecordSampleRate());
        mRecordChannel = static_cast<int>(cfg.GetRecordChannels());
        mRecordBitDepth = static_cast<int>(cfg.GetRecordBitDepth());

        // AAS声纹接口目标格式
        auto audioFormat = HalBridge::GetInstance().GetAudioFormat();
        mAasFormatConfig.mSampleRate = audioFormat.mSampleRate;
        mAasFormatConfig.mChannels = audioFormat.mChannels;
        mAasFormatConfig.mBitDepth = audioFormat.mBitDepth;

        // 1秒字节数(用于读循环缓冲区与录制时长校验)
        mHalOneSecBytes = AudioUtils::CalculateOneSecondBytes(audioFormat);
    }

    VoiceprintTask::~VoiceprintTask() {
        if (IsRunning()) {
            StopAndWait();
        }
    }

    static std::string BuildTimerName(const std::string &audioId, const std::string &suffix) {
        return "VoiceprintTask_" + audioId + "_" + suffix;
    }

    void VoiceprintTask::Start() {
        SetRunning(true);
        mReadTimerName = BuildTimerName(mAudioId, "Read");
        mTimeoutTimerName = BuildTimerName(mAudioId, "Timeout");
        TimerManager::GetInstance().Register(mReadTimerName);
        TimerManager::GetInstance().Register(mTimeoutTimerName);
        mPcmDataSize = 0;
        mPcmBuffer.clear();
        // 预分配内存(60s * 16kHz * 1ch * 2bytes ≈ 1.92MB)
        // 在Start时分配而非构造时, 避免任务排队等待期间占用内存
        mPcmBuffer.reserve(mHalOneSecBytes * DefaultRecordDurationSec);
        mWaitNotified = false;
        mIsTimeout.store(false, std::memory_order_release);
        StartTimeoutTimer();
        ScheduleNext();
        SLOG_INFO << "VoiceprintTask: started, audioId=" << mAudioId;
    }

    void VoiceprintTask::Stop() {
        if (!mRunning.exchange(false)) {
            return;
        }
        StopTimers();

        SLOG_INFO << "VoiceprintTask: stopping, audioId=" << mAudioId;
    }

    void VoiceprintTask::Cancel() {
        SLOG_INFO << "VoiceprintTask: canceling, audioId=" << mAudioId;
        BaseTask::Cancel();  // 设置取消标志并调用Stop()
        NotifyWaiter({Status {-1, "声纹录制已取消"}, {}, "", ""});
    }

    VoiceprintResult VoiceprintTask::StopAndWait(int waitTimeoutMs) {
        bool wasRunning = mRunning.exchange(false);
        if (!wasRunning && !IsComplete()) {
            // 60s超时已触发OnRecordingFinished，但AAS尚未回调，等待结果
            SLOG_INFO << "VoiceprintTask: StopAndWait waiting for AAS callback, audioId=" << mAudioId;
            std::unique_lock<std::mutex> lock(mWaitMutex);
            bool ok =
                mWaitCv.wait_for(lock, std::chrono::milliseconds(waitTimeoutMs), [this] { return mWaitNotified; });
            if (!ok) {
                SLOG_WARN << "VoiceprintTask: StopAndWait timeout, audioId=" << mAudioId;
                return {Status {-1, "声纹提取超时"}, {}, "", ""};
            }
            return mWaitResult;
        }

        StopTimers();

        // 如果AAS已完成, 直接返回缓存的结果
        if (IsComplete()) {
            std::lock_guard<std::mutex> lock(mWaitMutex);
            return mWaitResult;
        }

        // 停止读取循环, 触发AAS
        SLOG_INFO << "VoiceprintTask: StopAndWait, audioId=" << mAudioId;
        OnRecordingFinished();

        // 同步等待AAS结果
        {
            std::unique_lock<std::mutex> lock(mWaitMutex);
            bool ok =
                mWaitCv.wait_for(lock, std::chrono::milliseconds(waitTimeoutMs), [this] { return mWaitNotified; });
            if (!ok) {
                SLOG_WARN << "VoiceprintTask: StopAndWait timeout, audioId=" << mAudioId;
                return {Status {-1, "声纹提取超时"}, {}, "", ""};
            }
        }

        std::lock_guard<std::mutex> lock(mWaitMutex);
        return mWaitResult;
    }

    void VoiceprintTask::StopReadLoop() {
        mRunning.store(false);
    }

    void VoiceprintTask::StopTimers() {
        if (!mReadTimerName.empty()) {
            TimerManager::GetInstance().CancelByName(mReadTimerName);
        }
        if (!mTimeoutTimerName.empty()) {
            TimerManager::GetInstance().CancelByName(mTimeoutTimerName);
        }
    }

    void VoiceprintTask::DoReadCycle() {
        if (!IsRunning()) {
            return;
        }
        // 持有 mReadMutex 使 Stop/Cancel 可同步等待 ReadAudio 返回
        std::lock_guard<std::mutex> lock(mReadMutex);
        if (!IsRunning()) {
            return;
        }
        auto &hal = HalBridge::GetInstance();
        std::vector<uint8_t> buffer(static_cast<size_t>(mHalOneSecBytes));
        size_t bytesRead = hal.ReadAudio(buffer, mHalOneSecBytes);

        if (bytesRead == 0) {
            return;
        }
        // 无需重采样, 直接缓存
        mPcmBuffer.insert(mPcmBuffer.end(), buffer.begin(), buffer.begin() + static_cast<ssize_t>(bytesRead));
        mPcmDataSize += bytesRead;
    }

    void VoiceprintTask::ScheduleNext() {
        if (!IsRunning() || mReadTimerName.empty()) {
            return;
        }

        auto self = Self();
        auto* timerTask = WFTaskFactory::create_timer_task(
            mReadTimerName, 0, static_cast<long>(ReadIntervalMs) * 1000L, [self](WFTimerTask* task) {
                if (task->get_state() != WFT_STATE_SUCCESS) {
                    SLOG_DEBUG << "VoiceprintTask: read timer canceled or error, audioId=" << self->mAudioId;
                    return;
                }
                if (!self->IsRunning()) {
                    return;
                }
                self->DoReadCycle();

                if (self->IsRunning()) {
                    auto* goTask = WFTaskFactory::create_go_task("VoiceprintTask", [self]() { self->ScheduleNext(); });
                    auto* series = Workflow::create_series_work(goTask, nullptr);
                    series->start();
                }
            });

        timerTask->start();
    }

    void VoiceprintTask::OnRecordingFinished() {
        // 持锁等待读循环退出, 确保 mPcmBuffer/mPcmDataSize 不被 DoReadCycle 并发访问
        // 防止 SubmitToAas 中 std::move(mPcmBuffer) 与 DoReadCycle 的 insert 并发导致堆破坏
        std::lock_guard<std::mutex> lock(mReadMutex);

        SLOG_INFO << "VoiceprintTask: recording finished, pcmSize=" << mPcmDataSize << ", audioId=" << mAudioId;

        if (mPcmDataSize == 0) {
            NotifyResult({Status {-1, "未采集到音频数据"}, {}, "", ""});
            return;
        }
        auto limit = VoiceprintConfig::GetInstance().GetLimitRecordDurationSec();
        if (mPcmDataSize < mHalOneSecBytes * limit) {
            NotifyResult({Status {-1, "音频数据不足" + std::to_string(limit) + "秒"}, {}, "", ""});
            return;
        }

        // 将PCM数据写入WAV文件
        // WriteWavFile();

        // 提交PCM数据到AAS声纹提取(异步, 结果通过SendCallBack回调)
        SubmitToAas();
    }

    void VoiceprintTask::WriteWavFile() {
        auto filePath = GetVoiceprintFileName(mAccountId, mAudioId);
        if (!FileOpt::CreateDstDirectory(filePath)) {
            SLOG_ERROR << "VoiceprintTask: create voiceprint dir failed, path=" << filePath;
            return;
        }

        mFilePath = filePath;

        std::ofstream wavFile(mFilePath, std::ios::binary);
        if (!wavFile.is_open()) {
            SLOG_ERROR << "VoiceprintTask: open wav file failed, path=" << mFilePath;
            mFilePath.clear();
            return;
        }

        AudioUtilsConfig config;
        config.mSampleRate = mAasFormatConfig.mSampleRate;
        config.mChannels = mAasFormatConfig.mChannels;
        config.mBitDepth = mAasFormatConfig.mBitDepth;
        AudioUtils::WriteWavHeader(wavFile, config);

        wavFile.write(reinterpret_cast<const char*>(mPcmBuffer.data()),  // NOLINT
                      static_cast<std::streamsize>(mPcmBuffer.size()));
        AudioUtils::FinalizeWavFile(wavFile, static_cast<uint32_t>(mPcmBuffer.size()));
        wavFile.close();

        SLOG_INFO << "VoiceprintTask: wav file written, path=" << mFilePath << ", size=" << mPcmBuffer.size();
    }

    void VoiceprintTask::SubmitToAas() {
        // 构建AAS请求, 设置SendCallBack接收声纹提取结果
        auto self = Self();
        qifeng::aas::ResultInfo info;
        info.mData = std::move(mPcmBuffer);
        info.mConfig = mAasFormatConfig;

        // 设置AAS回调: StartSVJob完成后异步调用, 传回声纹特征
        info.mCallBack = [self](qifeng::aas::AasResult aasResult, qifeng::aas::BmsInfo) {
            self->OnAasResult(std::move(aasResult));
        };

        int ret = qifeng::aas::StartVoiceprintRegister(info);
        if (ret != 0) {
            SLOG_ERROR << "VoiceprintTask: StartSVJob failed, ret=" << ret;
            NotifyResult({Status {-1, "声纹任务启动失败"}, {}, "", mFilePath});
            return;
        }
        SLOG_INFO << "VoiceprintTask: submitting to AAS, audioId=" << mAudioId;
    }

    void VoiceprintTask::OnAasResult(qifeng::aas::AasResult aasResult) {
        SLOG_INFO << "VoiceprintTask: AAS callback, code=" << aasResult.code << " audioId=" << mAudioId;
        // 任务已被取消或已完成: 丢弃迟到的 AAS 回调, 防止回调干扰新任务(如误关 HAL)
        if (IsComplete()) {
            SLOG_INFO << "VoiceprintTask: AAS callback after cancel/complete, ignoring, audioId=" << mAudioId;
            return;
        }

        VoiceprintResult vpResult;
        if (aasResult.code != 0) {
            vpResult.mStatus = Status {aasResult.code, aasResult.message};
        } else {
            vpResult.mStatus = Status {};
            vpResult.mSvEmbedding = std::move(aasResult.svEmbedding);
            vpResult.mSvEmbeddingMd5 = std::move(aasResult.svEmbeddingMd5);
        }
        vpResult.mFilePath = std::move(mFilePath);

        NotifyResult(vpResult);
    }

    void VoiceprintTask::NotifyWaiter(const VoiceprintResult &result) {
        SetComplete();
        // 保存结果并通知StopAndWait的等待线程
        {
            std::lock_guard<std::mutex> lock(mWaitMutex);
            mWaitResult = result;
            mWaitNotified = true;
        }
        mWaitCv.notify_all();
    }

    void VoiceprintTask::NotifyResult(const VoiceprintResult &result) {
        SLOG_INFO << "VoiceprintTask: stop and notify result, status=" << result.mStatus.ToString()
                  << ", audioId=" << mAudioId;

        // 写数据库
        if (mCallback) {
            mCallback(result);
        }

        NotifyWaiter(result);
    }

    void VoiceprintTask::OnTimeout() {
        if (!mRunning.exchange(false)) {
            return;
        }
        // 标记已成功提交AAS
        mIsTimeout.store(true, std::memory_order_release);
        StopTimers();

        SLOG_INFO << "VoiceprintTask: timeout(" << DefaultRecordDurationSec << "s), audioId=" << mAudioId;
        OnRecordingFinished();
    }

    void VoiceprintTask::StartTimeoutTimer() {
        if (mTimeoutTimerName.empty()) {
            return;
        }
        auto self = Self();
        time_t timeoutSec = static_cast<time_t>(DefaultRecordDurationSec);
        auto* timerTask =
            WFTaskFactory::create_timer_task(mTimeoutTimerName, timeoutSec, 0L, [self](WFTimerTask* task) {
                if (task->get_state() != WFT_STATE_SUCCESS) {
                    SLOG_DEBUG << "VoiceprintTask: timeout timer canceled or error, audioId=" << self->mAudioId;
                    return;
                }
                self->OnTimeout();
            });
        timerTask->start();
    }

}  // namespace qifeng_ca
