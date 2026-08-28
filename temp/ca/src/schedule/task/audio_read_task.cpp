//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/meeting.pb.h"
#include "qifeng_framework/aas/audio_load.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"
#include "qifeng_framework/common/utils/time.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"

#include "common/audio/audio_utils.h"
#include "common/audio_enums.h"
#include "common/config/meeting_config.h"
#include "common/timer_manager.h"
#include "common/utils/file_name_generator.h"
#include "common/utils/symlink_manager.h"
#include "common/ws/realtime_dispatch_manager.h"
#include "common/ws/system_message_notifier.h"
#include "core/meeting/meeting_db_service.h"
#include "core/meeting/recording_service.h"
#include "dao/trans_dao.h"
#include "dao_managers/meeting_dao_manager.h"
#include "internal/aas/audio_qw_transcribe_manager.h"
#include "internal/aas/audio_transcribe_manager.h"
#include "internal/display_manager.h"
#include "internal/hal/hal_bridge.h"
#include "internal/recording_manager.h"
#include "internal/transcribe_handler.h"
#include "schedule/pcm/pcm_engine.h"
#include "schedule/task/audio_read_task.h"
#include "schedule/task/offline_trans_task.h"
#include "schedule/task/summary_task.h"
#include "schedule/task/task_db_helper.h"

namespace {

    // 将剩余毫秒数格式化为显示屏倒计时文本
    // 规则: >=1天显示"xx天", >=1小时显示"xx小时", >=1分钟显示"xx分钟", <1分钟显示"xx秒"
    std::string FormatCountdown(int64_t remainingMs) {
        int64_t sec = remainingMs / 1000;
        if (sec <= 0) {
            return "0秒";
        }
        int64_t days = sec / 86400;
        if (days > 0) {
            return std::to_string(days) + "天";
        }
        int64_t hours = sec / 3600;
        if (hours > 0) {
            return std::to_string(hours) + "小时";
        }
        int64_t minutes = sec / 60;
        if (minutes > 0) {
            return std::to_string(minutes) + "分钟";
        }
        return std::to_string(sec) + "秒";
    }

}  // namespace

namespace qifeng_ca {

    AudioReadTask::AudioReadTask(const std::string &audioId, uint64_t accountId) : BaseTask(audioId, accountId) {
        // 获取麦克风采样率、声道数、位深并推断时长
        auto audioFormat = HalBridge::GetInstance().GetAudioFormat();

        // 麦克风采样率、声道数、位深并推断时长
        mRecordSampleRate = static_cast<int>(audioFormat.mSampleRate);
        mRecordChannel = static_cast<int>(audioFormat.mChannels);
        mRecordBitDepth = static_cast<int>(audioFormat.mBitDepth);
        mEnergyCalc.Init(mRecordSampleRate, mRecordChannel, mRecordBitDepth);

        // 重采样格式、AAS格式配置
        mAasFormatConfig.mSampleRate = audioFormat.mSampleRate;
        mAasFormatConfig.mChannels = audioFormat.mChannels;
        mAasFormatConfig.mBitDepth = audioFormat.mBitDepth;
        mWaveformBuffer.resize(WaveformDisplayCount);

        AudioUtilsConfig halCfg {static_cast<uint32_t>(mRecordSampleRate), static_cast<uint16_t>(mRecordChannel),
                                 static_cast<uint16_t>(mRecordBitDepth)};
        mOneSecBytes = AudioUtils::CalculateOneSecondBytes(halCfg);

        // 时长上限
        auto LimitRecordTimeSec = MeetingConfig::GetInstance().GetLimitRecordTimeSec();
        mLimitBytes = static_cast<uint64_t>(LimitRecordTimeSec) * mOneSecBytes;
        mLimitTimeMs = LimitRecordTimeSec * 1000;
    }

    AudioReadTask::~AudioReadTask() {
        if (mDualWriter.IsOpen()) {
            mDualWriter.Finalize();
        }
        if (mProvider || mQwenProvider) {
            qifeng::aas::AasJobStop();
            mProvider.reset();
            mQwenProvider.reset();
            mHandler.reset();
        }
    }

    void AudioReadTask::Start() {
        SetRunning(true);
        ScopeExit exit([this]() {
            mNeedSummary = false;
            SetComplete();
        });
        if (!CheckRecording()) {
            return;
        }
        if (!StartAasTrans()) {
            return;
        }
        exit.Release();
        StartReadCycle();
        TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::Meeting,
                                        AudioStatusMsg::RecordingTranscribing);
    }

    bool AudioReadTask::CheckRecording() {
        auto &recordingMgr = RecordingManager::GetInstance();
        if (!recordingMgr.IsRecording()) {
            SLOG_ERROR << "AudioReadTask: recording not started, audioId=" << mAudioId;
            return false;
        }
        return true;
    }

    bool AudioReadTask::StartAasTrans() {
        // Paraformer provider: 实时每秒调用, 保证实时响应; 结果缓存并保持现有机制推送前端/落库
        auto paraProvider = std::make_shared<AudioStreamProvider>(mAudioId, mAccountId, true);
        mProvider = paraProvider;

        // QWen provider: 累计30s数据调用一次, 结果与DB记录匹配替换文本, 最终以QWen结果为准
        auto qwenProvider = std::make_shared<AudioQWStreamProvider>(mAudioId, mAccountId, true);
        mQwenProvider = qwenProvider;

        mHandler = std::make_shared<TranscribeHandler>(mAudioId, mAccountId, true);
        // QWen结果与DB记录整合合并器(独立模块, 负责匹配/替换/缓存/落库/推送)
        mQwenMerger = std::make_unique<QwenTransMerger>(mAudioId, mAccountId);

        // Paraformer结果: 缓存 + 走现有TranscribeHandler机制(落库+推送前端)
        auto handler = mHandler;
        auto self = Self();
        paraProvider->SetTransResultCallback(
            [handler, self](const std::string & /*aid*/, uint64_t /*accId*/, const qifeng::aas::AasResult &result) {
                self->OnParaformerResult(handler, result);
            });

        // QWen结果: 文本匹配替换后推送前端
        // 捕获merger副本(shared_ptr)而非依赖任务成员: 任务stop后最后一段QWen结果可能晚于
        // Flush/reset返回(如QWen比Paraformer先返回/在途请求), 回调仍持有merger,
        // 由merger内部mStopped兜底落库(见QwenTransMerger::OnQwenResult), 保证stop后QWen数据一定写入数据库
        auto merger = mQwenMerger;
        qwenProvider->SetTransResultCallback(
            [merger](const std::string & /*aid*/, uint64_t /*accId*/, const qifeng::aas::AasResult &result) {
                if (merger) {
                    merger->OnQwenResult(result);
                } else {
                    SLOG_WARN << "AudioReadTask: qwen merger released, result dropped";
                }
            });

        // 两个模型使用不同的启动接口, 停止统一走AasJobStop
        qifeng::aas::AasParaformerJobStart(std::static_pointer_cast<qifeng::aas::GetAudioBase>(paraProvider));
        qifeng::aas::AasQwenJobStart(std::static_pointer_cast<qifeng::aas::GetAudioBase>(qwenProvider));
        SLOG_INFO << "AudioReadTask: start AAS realtime trans (Paraformer+QWen), audioId=" << mAudioId;
        return true;
    }

    void AudioReadTask::StopAasTrans() {
        if (!mProvider && !mQwenProvider) {
            return;
        }
        // 信号end: 通知AAS不再有新数据(幂等, 已ended时直接跳过);
        // QWen provider在结束时冲刷剩余不足30s的数据, 确保所有音频均经过QWen处理
        if (mProvider && !mProvider->IsEnded()) {
            mProvider->SignalEnd();
        }
        if (mQwenProvider && !mQwenProvider->IsEnded()) {
            mQwenProvider->SignalEnd();
        }

        // 等待AAS线程消费完缓冲区剩余数据, 避免转写丢失
        auto startTime = GetTimeMs();
        int maxWaitMs = 2000;
        int waitedMs = 0;
        bool hasPending =
            (mProvider && mProvider->HasPendingData()) || (mQwenProvider && mQwenProvider->HasPendingData());

        while (hasPending && waitedMs < maxWaitMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            waitedMs += 100;
            hasPending =
                (mProvider && mProvider->HasPendingData()) || (mQwenProvider && mQwenProvider->HasPendingData());
        }
        if (waitedMs >= maxWaitMs) {
            SLOG_WARN << "AudioReadTask: buffer drain timeout, audioId=" << mAudioId
                      << " waitedMs=" << GetTimeMs() - startTime << ", hasPending=" << hasPending;
        }

        // 通知handler最终结果(自然结束和主动停止都需要)
        if (mHandler) {
            mHandler->NotifyFinal();
        }

        // 统一停止两个AAS job(Paraformer与QWen)
        qifeng::aas::AasJobStop();
        // QWen合并器最终冲刷: 推送暂存未匹配的QWen段(兜底, 不落库), 避免任务结束文本永久丢失
        if (mQwenMerger) {
            mQwenMerger->Flush();
        }
        mProvider.reset();
        mQwenProvider.reset();
        mHandler.reset();
        // QWen合并器随转写停止一并释放(内部状态/缓存不再需要)
        mQwenMerger.reset();
        SLOG_INFO << "AudioReadTask: stop AAS trans (Paraformer+QWen), audioId=" << mAudioId;
    }

    // Paraformer结果: 保持现有机制推送前端/落库(匹配基准为DB记录, 不做原始输出缓存)
    void AudioReadTask::OnParaformerResult(const std::shared_ptr<TranscribeHandler> &handler,
                                           const qifeng::aas::AasResult &result) {
        // 保持现有机制: TranscribeHandler负责落库并推送前端
        // (落库时FindDominatedDbRecords保证DB中每个时间段只保留一条稳定记录, 即QWen的匹配基准)
        if (handler) {
            handler->OnAasResult(result);
        }
    }

    void AudioReadTask::StartReadCycle() {
        if (!OnRecordingStart()) {
            SLOG_ERROR << "AudioReadTask: open wav file failed, audioId=" << mAudioId;
            mNeedSummary = false;
            SetComplete();
            return;
        }
        mEnergyDispatchTimerName = "EnergyDispatch_" + mAudioId;
        TimerManager::GetInstance().Register(TimerName::AudioReadTimer);
        TimerManager::GetInstance().Register(mEnergyDispatchTimerName);
        SLOG_INFO << "AudioReadTask: started, audioId=" << mAudioId;
        ScheduleNext();
    }

    void AudioReadTask::Stop() {
        if (!mRunning.exchange(false)) {
            return;
        }
        SLOG_INFO << "AudioReadTask: stopping, audioId=" << mAudioId;
        RecordingManager::GetInstance().StopRecording(mAudioId);
        StopAasTrans();
    }

    void AudioReadTask::After() {
        if (!mNeedSummary) {
            return;
        }
        TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::WaitSummary,
                                        AudioStatusMsg::WaitMeetingSummary);
        SubmitSummaryTaskInternal();
    }

    void AudioReadTask::DoReadCycle() {
        auto &recMgr = RecordingManager::GetInstance();
        if (!recMgr.IsRecording()) {
            OnRecordingStopped();
            return;
        }

        // 暂停中: 不读HAL数据, 暂停AAS provider(音频时长自然暂停)
        if (recMgr.IsPaused()) {
            PauseProvider();
            return;
        }
        // 恢复: 重启AAS provider
        ResumeProvider();

        auto &hal = HalBridge::GetInstance();
        auto secBytes = mOneSecBytes * 5;  // 读取5秒音频
        std::vector<uint8_t> buffer(static_cast<size_t>(secBytes));
        size_t bytesRead = hal.ReadAudio(buffer, secBytes);

        if (bytesRead == 0) {
            SLOG_WARN << "HAL ReadAudio bytesRead is zero";
            if (hal.IsRecordingUnavailable() || hal.IsRecordingUninitialized()) {
                SLOG_WARN << "HAL recording unavailable or uninitialized, audioId=" << mAudioId;
                OnRecordingPuased();
            }
            return;
        }

        mTotalHalBytes += bytesRead;
        AudioUtilsConfig halReadCfg {static_cast<uint32_t>(mRecordSampleRate), static_cast<uint16_t>(mRecordChannel),
                                     static_cast<uint16_t>(mRecordBitDepth)};
        int durationMs = AudioUtils::CalculateDurationMs(mTotalHalBytes, halReadCfg);
        SLOG_INFO << "AudioReadTask: HAL read stats, audioId=" << mAudioId << " bytesRead=" << bytesRead
                  << " totalHalBytes=" << mTotalHalBytes << " durationMs=" << durationMs;

        mAudioBuffer.insert(mAudioBuffer.end(), buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(bytesRead));
        TryProcessAlignedAudio();
    }

    void AudioReadTask::ScheduleNext() {
        if (!mRunning.load()) {
            SLOG_INFO << "AudioReadTask: not running, audioId=" << mAudioId;
            return;
        }

        auto self = Self();

        // 使用命名定时器(支持优雅退出时cancel_by_name)
        static constexpr long ReadIntervalNs = ReadIntervalMs * 1000000L;  // 100ms
        auto* timerTask = WFTaskFactory::create_timer_task(
            std::string(TimerName::AudioReadTimer), 0, ReadIntervalNs, [self](WFTimerTask* task) {
                // 检查timer状态, 防止取消后异常回调
                if (task->get_state()) {
                    SLOG_ERROR << "AudioReadTimer: timer callback, state: " << task->get_state()
                               << ", error: " << task->get_error();
                    return;
                }

                self->DoReadCycle();

                if (self->IsRunning()) {
                    auto* goTask = WFTaskFactory::create_go_task("AudioReadTask", [self]() { self->ScheduleNext(); });
                    auto* series = Workflow::create_series_work(goTask, nullptr);
                    series->start();
                }
            });

        timerTask->start();
    }

    bool AudioReadTask::OnRecordingStart() {
        OpenWavFile();
        return mHeaderWritten;
    }

    void AudioReadTask::OnRecordingPuased() {
        RecordStopRequest req;
        Empty resp;
        req.set_audio_id(mAudioId);
        req.set_account_id(mAccountId);
        auto service = RecordingService();
        service.PauseRecording(req, &resp);
    }

    void AudioReadTask::OnRecordingStopped() {
        // 统一通过TimerManager取消读循环定时器
        TimerManager::GetInstance().CancelByName(TimerName::AudioReadTimer);
        FlushRemainingAudio();
        FinalizeWavFile();
        // 限制时长上限
        int32_t totalTimeMs = mLimitReached ? mLimitTimeMs : CalculateRecordingTime();

        if (!mEnergyDispatchTimerName.empty()) {
            TimerManager::GetInstance().CancelByName(mEnergyDispatchTimerName);
        }

        // 检查录音时长是否太短, 太短则删除录音文件
        int32_t minMs = MeetingConfig::GetInstance().GetMinRecordingSeconds() * 1000;
        if (totalTimeMs < minMs) {
            // 标记为删除中, 主要是为规避SearchAudio时因为时序问题导致出现删除中的音频
            TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::Deleting, AudioStatusMsg::Deleting);

            auto &notifier = SystemMessageNotifier::GetInstance();
            notifier.SendFileMissing(mAudioId, mSystemMessageNotifierAll ? 0 : mAccountId);

            StopAasTrans();

            Status st = MeetingDBService::DeleteAudioCascade(mAccountId, mAudioId);
            if (!st.IsSuccess()) {
                SLOG_WARN << "AudioReadTask: delete short recording failed, audioId=" << mAudioId
                          << " status=" << st.ToString();
            }

            mRunning.store(false);
            mNeedSummary = false;
            SetComplete();
            SLOG_INFO << "AudioReadTask: recording too short, deleted, audioId=" << mAudioId
                      << " totalTimeMs=" << totalTimeMs << " minMs=" << minMs;
            return;
        }

        auto &daoMg = MeetingDaoManager::GetInstance();
        daoMg.UpdateTotalTime(mAccountId, mAudioId, totalTimeMs);

        StopAasTrans();

        // 迁移: 校验软连接文件后更新DB路径, 删除data下文件
        MigrateToSymlinkAfterRecording();

        mRunning.store(false);
        SetComplete();
        SLOG_INFO << "AudioReadTask: recording stopped, audioId=" << mAudioId << " totalTimeMs=" << totalTimeMs
                  << " dataSize=" << mDataSize;
    }

    // 检查实时转写数据丢失,超过阈值则转为离线转写任务,否则提交总结任务
    bool AudioReadTask::CheckTransDataLoss() {
        static constexpr int DataLossThresholdSec = 5;

        TransDao dao;
        auto transList = dao.GetByAudioId(mAccountId, mAudioId);
        if (transList.size() < 2) {
            return false;
        }
        // 按时间排序
        std::sort(transList.begin(), transList.end(),
                  [](const models::Trans &lhs, const models::Trans &rhs) { return lhs.mStartTime < rhs.mStartTime; });

        for (size_t i = 1; i < transList.size(); ++i) {
            int32_t prevEnd = transList[i - 1].mEndTime;
            int32_t currStart = transList[i].mStartTime;
            int32_t gapMs = currStart - prevEnd;
            if (gapMs > DataLossThresholdSec * 1000) {
                SLOG_WARN << "AudioReadTask: data loss detected, gap=" << gapMs << "ms audioId=" << mAudioId;
                return true;
            }
        }
        return false;
    }

    // 离线转写任务
    void AudioReadTask::SubmitOfflineTransTask() {
        auto &symMgr = SymlinkManager::GetInstance();
        std::string srcPath = symMgr.ResolveData2Path(mWavFilePath);
        std::string taskFilePath = symMgr.ResolveData2Path(GetAudioFilePath(mAccountId, mAudioId));
        auto offlineTask = std::make_shared<OfflineTransTask>(mAudioId, mAccountId, srcPath, taskFilePath, true);
        PcmEngine::GetInstance().Submit(offlineTask);
        SLOG_INFO << "AudioReadTask: data loss detected, converted to offline trans, audioId=" << mAudioId
                  << " srcPath=" << srcPath << " taskFilePath=" << taskFilePath;
    }

    // 纪要总结任务
    void AudioReadTask::SubmitSummaryTaskInternal() {
        // 直接创建SummaryTask并提交到TaskScheduler(链式触发)
        auto summaryTask = std::make_shared<SummaryTask>(mAudioId, mAccountId);
        PcmEngine::GetInstance().Submit(summaryTask);
        SLOG_INFO << "AudioReadTask: summary task submitted, audioId=" << mAudioId;
    }

    void AudioReadTask::OpenWavFile() {
        mWavFilePath = RecordingManager::GetInstance().GetWavFilePath();

        AudioUtilsConfig audioConfig;
        audioConfig.mSampleRate = static_cast<uint32_t>(mAasFormatConfig.mSampleRate);
        audioConfig.mChannels = static_cast<uint16_t>(mAasFormatConfig.mChannels);
        audioConfig.mBitDepth = static_cast<uint16_t>(mAasFormatConfig.mBitDepth);

        if (!mDualWriter.Open(mWavFilePath, audioConfig)) {
            SLOG_ERROR << "AudioReadTask: open wav file failed, path=" << mWavFilePath;
            return;
        }
        mHeaderWritten = true;
    }

    // 录音结束后: 校验软连接文件状态正常后, 更新DB file_name为软连接路径, 删除data下文件
    void AudioReadTask::MigrateToSymlinkAfterRecording() {
        auto &mgr = SymlinkManager::GetInstance();
        if (!mgr.IsData2Available()) {
            return;
        }
        if (!mgr.IsSymlinkValid()) {
            SLOG_WARN << "AudioReadTask: symlink invalid, skip migrate, audioId=" << mAudioId;
            return;
        }
        std::string symlinkPath = mDualWriter.GetSymlinkPath();
        if (symlinkPath.empty()) {
            return;
        }
        std::error_code ec;
        if (!std::filesystem::exists(symlinkPath, ec)) {
            SLOG_WARN << "AudioReadTask: symlink file not exist, skip migrate, path=" << symlinkPath;
            return;
        }
        // 校验主路径和软连接文件大小一致(双写应为相同大小)
        auto srcSize = std::filesystem::file_size(mWavFilePath, ec);
        if (ec) {
            return;
        }
        auto dstSize = std::filesystem::file_size(symlinkPath, ec);
        if (ec || srcSize != dstSize) {
            SLOG_ERROR << "AudioReadTask: size mismatch, src=" << srcSize << " dst=" << dstSize
                       << ", skip migrate, audioId=" << mAudioId;
            return;
        }
        // 更新DB file_name为软连接路径
        auto &daoMg = MeetingDaoManager::GetInstance();
        if (!daoMg.UpdateFileName(mAccountId, mAudioId, symlinkPath)) {
            SLOG_ERROR << "AudioReadTask: update file_name failed, audioId=" << mAudioId;
            return;
        }
        SLOG_INFO << "AudioReadTask: DB file_name updated, path=" << symlinkPath;
        // 删除data目录下的原始文件(双写完成后不再需要)
        std::filesystem::remove(mWavFilePath, ec);
        if (ec) {
            SLOG_WARN << "AudioReadTask: remove source file failed, path=" << mWavFilePath << " error=" << ec.message();
        } else {
            SLOG_INFO << "AudioReadTask: source file removed, path=" << mWavFilePath;
        }
    }

    void AudioReadTask::WriteWavData(const uint8_t* data, size_t len) {
        if (!mHeaderWritten) {
            OnRecordingStart();
        }
        if (!mDualWriter.IsOpen()) {
            return;
        }
        mDualWriter.Write(data, len);
        mDataSize = mDualWriter.GetDataSize();
    }

    // 按已写入WAV文件的数据量换算时长(数据域), 保证total_time与音频文件实际时长一致
    int32_t AudioReadTask::CalculateRecordingTime() const {
        if (mDataSize == 0) {
            return 0;
        }
        AudioUtilsConfig wavCfg {static_cast<uint32_t>(mAasFormatConfig.mSampleRate),
                                 static_cast<uint16_t>(mAasFormatConfig.mChannels),
                                 static_cast<uint16_t>(mAasFormatConfig.mBitDepth)};
        return AudioUtils::CalculateDurationMs(mDataSize, wavCfg);
    }

    void AudioReadTask::UpdateTotalTimeToDb() {
        int64_t nowMs = static_cast<int64_t>(GetTimeMs());
        if (nowMs - mLastTotalTimeUpdate < TotalTimeUpdateIntervalMs) {
            return;
        }

        int32_t recordingTime = CalculateRecordingTime();
        if (recordingTime <= 0) {
            return;
        }

        // 达到上限后时长以构造时冻结的精确值为准(永不超限)
        if (mLimitReached) {
            recordingTime = mLimitTimeMs;
        }

        auto &daoMgr = MeetingDaoManager::GetInstance();
        bool ok = daoMgr.UpdateTotalTime(mAccountId, mAudioId, recordingTime);
        if (!ok) {
            SLOG_ERROR << "AudioReadTask: update total_time failed, audioId=" << mAudioId;
            return;
        }

        mLastTotalTimeUpdate = nowMs;
        SLOG_DEBUG << "AudioReadTask: updated total_time=" << recordingTime << "ms, audioId=" << mAudioId;
    }

    // 录音达到上限: 提示显示屏+通知前端+主动RecordStop
    void AudioReadTask::HandleRecordLimitReached() {
        if (mLimitReached) {
            return;
        }
        mLimitReached = true;

        DisplayManager::GetInstance().ShowMeetingTimeOutError();  // 显示屏超时提示
        // 不能在RecordStop之后发送，因为可能导致前端未提前感知不足15s问题而获取"文件、笔记"等从而出现xxx错误提示
        SystemMessageNotifier::GetInstance().SendMeetingTimeOut(mAudioId, mSystemMessageNotifierAll ? 0 : mAccountId);

        RecordStopRequest req;
        req.set_account_id(mAccountId);
        req.set_audio_id(mAudioId);

        Empty resp;
        RecordingService().RecordStop(req, &resp);
        SLOG_INFO << "AudioReadTask: RecordStop called, limit time: " << mLimitTimeMs / 1000
                  << "s, audioId=" << mAudioId;
    }

    void AudioReadTask::FinalizeWavFile() {
        if (!mDualWriter.IsOpen()) {
            return;
        }
        mDualWriter.Finalize();
        mDataSize = mDualWriter.GetDataSize();
        SLOG_INFO << "AudioReadTask: wav finalized, dataSize=" << mDataSize;
    }

    void AudioReadTask::ForwardToAas(const uint8_t* data, size_t len) {
        if (!mProvider && !mQwenProvider) {
            SLOG_WARN << "AudioReadTask: providers not available, audioId=" << mAudioId;
            return;
        }
        if (mProvider) {
            mProvider->PushAudioData(data, len);
        }
        if (mQwenProvider) {
            mQwenProvider->PushAudioData(data, len);
        }
    }

    void AudioReadTask::PauseProvider() {
        if (!mProvider) {
            return;
        }
        // 首次进入暂停: 暂停provider并记录开始时间
        if (!mProviderPaused) {
            HandleFirstPause();
            return;
        }
        // 暂停超时: 直接停止会议
        int64_t maxPauseMs = static_cast<int64_t>(MeetingConfig::GetInstance().GetMaxPauseDurationSec()) * 1000;
        int64_t elapsedMs = static_cast<int64_t>(GetTimeMs()) - mPauseStartTimeMs;
        if (elapsedMs >= maxPauseMs) {
            HandlePauseTimeout(maxPauseMs, elapsedMs);
            return;
        }
        // 限速刷新暂停倒计时显示屏
        UpdatePauseCountdown(maxPauseMs, elapsedMs);
    }

    void AudioReadTask::HandleFirstPause() {
        mProvider->Pause();
        mProviderPaused = true;
        // 取消任务
        if (!mEnergyDispatchTimerName.empty()) {
            TimerManager::GetInstance().CancelByName(mEnergyDispatchTimerName);
        }
        mPauseStartTimeMs = static_cast<int64_t>(GetTimeMs());
        // 暂停时向显示屏发送全52波形, 表示暂停状态
        std::vector<int32_t> pauseWaveform(WaveformDisplayCount, 52);
        DisplayManager::GetInstance().UpdateWaveform(pauseWaveform);
        // 清理波形缓冲区, 保证静音后重新开始
        {
            std::lock_guard<std::mutex> lock(mWaveformMutex);
            mWaveformBuffer.clear();
            mWaveformBuffer.resize(WaveformDisplayCount);
        }
        SLOG_INFO << "AudioReadTask: AAS provider paused, audioId=" << mAudioId;
        TranscribeHandler::PausedMessage(mAudioId);  // 发送前端暂停消息
    }

    void AudioReadTask::HandlePauseTimeout(int64_t maxPauseMs, int64_t elapsedMs) {
        SLOG_WARN << "AudioReadTask: pause timeout, stopping meeting, audioId=" << mAudioId
                  << " elapsedMs=" << elapsedMs << " maxMs=" << maxPauseMs;
        DisplayManager::GetInstance().ShowMeetingPauseError();  // 显示屏暂停提示
        SystemMessageNotifier::GetInstance().SendPausedTimeOut(mAudioId, mSystemMessageNotifierAll ? 0 : mAccountId);
        RecordStopRequest req;
        req.set_account_id(mAccountId);
        req.set_audio_id(mAudioId);
        Empty resp;
        RecordingService().RecordStop(req, &resp);
    }

    // 可优化到DisplayManager中
    void AudioReadTask::UpdatePauseCountdown(int64_t maxPauseMs, int64_t elapsedMs) {
        // 限速500ms + 仅倒计时文本变化时才刷新显示屏
        int64_t nowMs = static_cast<int64_t>(GetTimeMs());
        if (nowMs - mLastCountdownUpdateMs < 500) {
            return;
        }
        mLastCountdownUpdateMs = nowMs;
        int64_t remainingMs = maxPauseMs - elapsedMs;
        std::string countdownText = FormatCountdown(remainingMs);
        auto popupId = DisplayManager::GetInstance().GetCurrentPopup();
        // 校验GetCurrentPopup是为了能在弹窗时及时刷新弹窗
        if (countdownText == mLastCountdownText && popupId == 0) {
            return;
        }
        mLastCountdownText = countdownText;
        if (popupId != 0) {
            mLastCountdownText.clear();  // 为了能稳定触发下一次刷新弹窗（够恶心的耦合）
        }
        MeetingMetrics countdownMetrics;
        countdownMetrics.mAudioStatus = 1;   // 暂停态
        countdownMetrics.mCountDownTip = 1;  // 1: 有倒计时卡片
        countdownMetrics.mCountDown = std::move(countdownText);
        DisplayManager::GetInstance().UpdateMeetingInfo(countdownMetrics);
    }

    void AudioReadTask::ResumeProvider() {
        if (!mProviderPaused) {
            return;
        }
        if (mProvider) {
            mProvider->Resume();
            TranscribeHandler::ResumeMessage(mAudioId);  // 发送前端恢复消息
        }
        mProviderPaused = false;
        mLastCountdownText = "";
        SLOG_INFO << "AudioReadTask: AAS provider resumed, audioId=" << mAudioId;
    }

    // 尝试截断缓存中对齐1秒整数倍的音频数据并处理
    void AudioReadTask::TryProcessAlignedAudio() {
        if (mOneSecBytes == 0 || mAudioBuffer.size() < mOneSecBytes) {
            return;
        }

        size_t alignedSize = (mAudioBuffer.size() / mOneSecBytes) * mOneSecBytes;
        if (alignedSize == 0) {
            return;
        }

        std::vector<uint8_t> alignedData(mAudioBuffer.begin(),
                                         mAudioBuffer.begin() + static_cast<ptrdiff_t>(alignedSize));
        mAudioBuffer.erase(mAudioBuffer.begin(), mAudioBuffer.begin() + static_cast<ptrdiff_t>(alignedSize));

        DispatchAudioData(alignedData.data(), alignedData.size());
    }

    // 处理音频数据(重采样判断 + 写WAV + 推AAS + 更新波形)
    // 时长硬限: 每批按已处理字节数截断, WAV内容时长精确等于配置上限(秒), 永不超过
    void AudioReadTask::DispatchAudioData(const uint8_t* data, size_t len) {
        if (len == 0) {
            return;
        }
        // 上限字节数
        const uint64_t limitBytes = mLimitBytes;
        if (limitBytes > 0) {
            if (mProcessedBytes >= limitBytes) {
                // 已达上限: 丢弃本批数据
                SLOG_WARN << "AudioReadTask: limit reached, drop audio data, audioId=" << mAudioId
                          << " processedBytes=" << mProcessedBytes << " limitBytes=" << limitBytes;
                return;
            }
            size_t allowed = static_cast<size_t>(limitBytes - mProcessedBytes);
            if (len > allowed) {
                len = allowed;
                SLOG_WARN << "AudioReadTask: limit reached, truncated audio data, audioId=" << mAudioId
                          << " processedBytes=" << mProcessedBytes << " limitBytes=" << limitBytes
                          << " truncatedBytes=" << (len - allowed) << " writeBytes=" << len;
            }
        }

        mProcessedBytes += len;
        WriteWavData(data, len);
        ForwardToAas(data, len);
        UpdateWaveform(data, len);
        UpdateTotalTimeToDb();

        // 恰好写满上限: 残余缓存(不足1秒)在FlushRemainingAudio中丢弃
        if (limitBytes > 0 && mProcessedBytes >= limitBytes) {
            HandleRecordLimitReached();
        }
    }

    // 录音结束时刷新缓存中不足1秒的残余数据
    void AudioReadTask::FlushRemainingAudio() {
        if (mAudioBuffer.empty()) {
            return;
        }

        // 已达时长上限: 丢弃残余, 保证最终时长精确等于配置上限
        if (mLimitBytes > 0 && mProcessedBytes >= mLimitBytes) {
            SLOG_INFO << "AudioReadTask: limit reached, drop remaining audio, audioId=" << mAudioId
                      << " remainingBytes=" << mAudioBuffer.size();
            mAudioBuffer.clear();
            return;
        }

        SLOG_INFO << "AudioReadTask: flush remaining audio, audioId=" << mAudioId
                  << " remainingBytes=" << mAudioBuffer.size();
        DispatchAudioData(mAudioBuffer.data(), mAudioBuffer.size());
        mAudioBuffer.clear();
        SLOG_INFO << "AudioReadTask END: flush remaining audio, audioId=" << mAudioId;
    }

    // 修复未正常关闭的WAV文件(如：断电重启后调用)
    bool AudioReadTask::RepairWavFile(const std::string &filePath) {
        if (filePath.empty()) {
            return false;
        }

        if (!std::filesystem::exists(filePath)) {
            SLOG_WARN << "RepairWavFile: file not exist, path=" << filePath;
            return false;
        }

        auto fileSize = std::filesystem::file_size(filePath);
        if (fileSize <= AudioUtils::WavHeaderSize) {
            SLOG_WARN << "RepairWavFile: file too small, size=" << fileSize;
            return false;
        }

        uint64_t dataSize = fileSize - AudioUtils::WavHeaderSize;

        std::fstream file(filePath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            SLOG_ERROR << "RepairWavFile: cannot open file, path=" << filePath;
            return false;
        }

        AudioUtils::FinalizeWavFile(file, dataSize);
        file.close();
        SLOG_INFO << "RepairWavFile: repaired, path=" << filePath << " dataSize=" << dataSize;
        return true;
    }

    void AudioReadTask::UpdateWaveform(const uint8_t* data, size_t len) {
        // 记录处理前的总帧数, 用于计算本次新增帧数
        int prevTotal = mEnergyCalc.GetTotalFrameCount();
        mEnergyCalc.ProcessAudioData(data, len);
        int numNew = mEnergyCalc.GetTotalFrameCount() - prevTotal;

        if (numNew <= 0) {
            DisplayManager::GetInstance().RefreshMeetingPage();
            return;
        }

        auto energyArr = mEnergyCalc.GetEnergyArray();
        // energyArr为 newest-first, 新增帧位于前 numNew 个; 取出并反转为时间序(oldest-first)
        std::vector<int32_t> newFrames;
        newFrames.reserve(static_cast<size_t>(numNew));
        for (int i = numNew - 1; i >= 0; --i) {
            newFrames.push_back(energyArr[static_cast<size_t>(i)]);
        }

        // 无音频输入检测: 按新增帧累计连续零能量帧, 达到阈值(100帧=10s)置无音频输入
        UpdateSilentDetection(newFrames);

        // 显示屏逐点刷新(100ms间隔), 前端WS分发保持原逻辑
        ScheduleDisplayRefresh(newFrames);
        ScheduleEnergyDispatch(energyArr);
        DisplayManager::GetInstance().RefreshMeetingPage();
    }

    void AudioReadTask::UpdateSilentDetection(const std::vector<int32_t> &newFrames) {
        // 每帧100ms, 100帧=10s连续无声判定为无音频输入
        static constexpr int SilentThresholdFrames = 100;
        auto &recMgr = RecordingManager::GetInstance();
        for (int32_t energy : newFrames) {
            if (energy > 0) {
                if (mSilentFrameCount > 0) {
                    mSilentFrameCount = 0;
                    recMgr.SetNoAudioInput(false);
                }
                return;
            }
            if (++mSilentFrameCount >= SilentThresholdFrames) {
                if (!recMgr.IsNoAudioInput()) {
                    recMgr.SetNoAudioInput(true);
                    SLOG_WARN << "AudioReadTask: no audio input detected, audioId=" << mAudioId;
                }
                return;
            }
        }
    }

    // 构建显示屏波形数据(调用者需持有mWaveformMutex)
    // 1. 正向填充: 从索引0开始, 旧数据在前(显示屏左侧), 新数据在后(显示屏右侧)
    // 2. 能量值上限100, 显示屏上限50: 除以2
    std::vector<int32_t> AudioReadTask::BuildDisplayBufferLocked() {
        std::vector<int32_t> displayBuf(WaveformDisplayCount, 0);
        size_t bufLen = std::min(mWaveformBuffer.size(), WaveformDisplayCount);
        for (size_t i = 0; i < bufLen; ++i) {
            displayBuf[i] = mWaveformBuffer[i] / 2;
        }
        return displayBuf;
    }

    void AudioReadTask::PushDisplayFrame(int32_t energy) {
        std::vector<int32_t> displayBuf;
        {
            std::lock_guard<std::mutex> lock(mWaveformMutex);
            mWaveformBuffer.push_back(energy);
            if (mWaveformBuffer.size() > WaveformDisplayCount) {
                mWaveformBuffer.erase(mWaveformBuffer.begin());
            }
            displayBuf = BuildDisplayBufferLocked();
        }
        DisplayManager::GetInstance().UpdateWaveform(displayBuf);
    }

    void AudioReadTask::ScheduleDisplayRefresh(const std::vector<int32_t> &newFrames) {
        if (newFrames.empty() || mEnergyDispatchTimerName.empty()) {
            return;
        }
        int64_t intervalNs = DisplayRefreshIntervalMs * 1000000LL;
        auto self = Self();

        for (size_t i = 0; i < newFrames.size(); ++i) {
            int64_t delayNs = static_cast<int64_t>(i) * intervalNs;
            auto* timerTask = WFTaskFactory::create_timer_task(mEnergyDispatchTimerName, 0, delayNs,
                                                               [self, energy = newFrames[i]](WFTimerTask* task) {
                                                                   if (task->get_state() || !self->IsRunning()) {
                                                                       return;
                                                                   }
                                                                   self->PushDisplayFrame(energy);
                                                               });
            timerTask->start();
        }
    }

    void AudioReadTask::ScheduleEnergyDispatch(const std::vector<int32_t> &energyArr) {
        size_t count = energyArr.size() > 15 ? 15 : energyArr.size();
        if (count == 0 || mEnergyDispatchTimerName.empty()) {
            return;
        }

        // 将1s拆成多个点, 每个点延迟intervalNs后发送
        int64_t intervalNs = 1000 * 1000000LL / static_cast<int64_t>(count);
        auto self = Self();

        for (size_t i = 0; i < count; ++i) {
            int64_t delayNs = static_cast<int64_t>(i) * intervalNs;
            auto* timerTask = WFTaskFactory::create_timer_task(mEnergyDispatchTimerName, 0, delayNs,
                                                               [self, energy = energyArr[i]](WFTimerTask* task) {
                                                                   if (task->get_state()) {
                                                                       return;
                                                                   }
                                                                   self->PushEnergyData(energy);
                                                               });
            timerTask->start();
        }
    }

    void AudioReadTask::PushEnergyData(const int32_t volumeNum) {
        std::string json = R"({"heart":false,"volume":true,"volumeNum":)" + std::to_string(volumeNum) + "}";

        auto &dispatchMgr = RealtimeDispatchManager::GetInstance();
        bool ok = dispatchMgr.Distribute(json, EClientType::kTranscribe);
        if (!ok) {
            SLOG_DEBUG << "AudioReadTask: no ws clients for energy data, audioId=" << mAudioId;
        }
    }

}  // namespace qifeng_ca
