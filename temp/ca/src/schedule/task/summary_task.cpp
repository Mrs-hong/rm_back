//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <chrono>
#include <ctime>
#include <malloc.h>
#include <thread>
#include <variant>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"
#include "qifeng_framework/common/utils/time.h"
#include "qifeng_framework/lms/metting.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"

#include "common/audio_enums.h"
#include "common/status.h"
#include "common/timer_manager.h"
#include "dao/note_dao.h"
#include "dao_managers/meeting_dao_manager.h"
#include "internal/lms/lifecycle.h"
#include "internal/lms/lms_hm_model_manager.h"
#include "internal/summary_callback.h"
#include "schedule/task/summary_task.h"
#include "schedule/task/task_db_helper.h"

namespace qifeng_ca {

    // 将LMS SummaryError映射为任务Status
    static Status MapErrorToStatus(const qifeng::lms::SummaryError &err) {
        switch (err.code) {
            case qifeng::lms::SummaryError::StateCode::INTERRUPTED:
                return Status {1, std::string(SummaryMsg::ManualStopInfer)};  // 非错误
            case qifeng::lms::SummaryError::StateCode::INVALID_DATA:
                return Status {-1, std::string(SummaryMsg::InputTooShort)};
            case qifeng::lms::SummaryError::StateCode::SUMMARY_ERROR:
                return Status {-1, std::string(SummaryMsg::ChainError)};
            case qifeng::lms::SummaryError::StateCode::INFERENCE_ERROR:
                return Status {-1, std::string(SummaryMsg::LmsConnectionError)};
            case qifeng::lms::SummaryError::StateCode::OUTPUT_TOO_LONG:
                return Status {-1, std::string(SummaryMsg::OutputTooLong)};
            case qifeng::lms::SummaryError::StateCode::INPUT_TOO_SHORT:
                return Status {-1, std::string(SummaryMsg::InputTooShort)};
            case qifeng::lms::SummaryError::StateCode::INPUT_TOO_LONG:
                return Status {-1, std::string(SummaryMsg::InputTooLong)};
            default:
                return Status {-1, std::string(SummaryMsg::UnknownError)};
        }
    }

    // 将关键词列表拼接为逗号分隔的字符串(用于回写DB)
    static std::string JoinKeywords(const std::vector<std::string> &keywords) {
        std::string result;
        for (const auto &kw : keywords) {
            if (!result.empty()) {
                result += ",";
            }
            result += kw;
        }
        return result;
    }

    // 构建转写文本内容(用于纪要总结)
    static std::string BuildTransContent(uint64_t accountId, const std::string &audioId) {
        auto &daoMg = MeetingDaoManager::GetInstance();
        auto transList = daoMg.GetTransContent(accountId, audioId);

        std::string content;
        for (const auto &trans : transList) {
            if (!content.empty()) {
                content += "\n";
            }
            content += trans.mContent;
        }
        return content;
    }

    // 格式化录音时间戳为 "YYYY-MM-DD HH:MM"
    static std::string FormatRecordingTime(int64_t timestamp) {
        if (timestamp <= 0) {
            return "";
        }
        time_t timeT = static_cast<time_t>(timestamp / 1000);
        struct tm tmBuf {};
        localtime_r(&timeT, &tmBuf);

        std::array<char, 32> buf {};
        strftime(buf.data(), buf.size(), "%Y-%m-%d %H:%M", &tmBuf);
        return std::string(buf.data());
    }

    SummaryTask::SummaryTask(const std::string &audioId, uint64_t accountId) : BaseTask(audioId, accountId) {
    }

    SummaryTask::~SummaryTask() {
        if (IsRunning()) {
            Cancel();
        }
        malloc_trim(0);  // 主动回收内存
    }

    void SummaryTask::Start() {
        SetRunning(true);
        mSumStartTime = GetTimeMs();
        // 若未通过SetInput设置输入, 则从DB构建
        if (mInput.mText.empty()) {
            BuildInputFromDb();
        }
        if (mInput.mText.empty()) {
            SLOG_WARN << "SummaryTask: trans content empty, audioId=" << mAudioId;
            NotifyResult({Status {-1, std::string(SummaryMsg::InputTooShort)}, "", ""});
            return;
        }
        // 启动超时定时器(40分钟)
        mTimeoutTimerName = "SummaryTask_" + mAudioId + "_Timeout";
        TimerManager::GetInstance().Register(mTimeoutTimerName);
        StartTimeoutTimer();
        // 初始化进度: 引擎在Summarize开始时上报预估总耗时(经BuildProgressObserver校准mEstimatedTotalMs),
        // 定时器按该总耗时推算进度兜底(引擎当前仅在上报一次预估耗时, 无周期回调)
        mLastProgress.store(0, std::memory_order_release);
        mExtraTimeAdded.store(false, std::memory_order_release);
        mEstimatedTotalMs.store(EstimateTotalMsByTextLen(mInput.mText.length()), std::memory_order_release);
        mProgressTimerName = "SummaryTask_" + mAudioId + "_Progress";
        TimerManager::GetInstance().Register(mProgressTimerName);
        StartProgressTimer();
        auto &daoMg = MeetingDaoManager::GetInstance();
        daoMg.UpdateSumStartTime(mAccountId, mAudioId, static_cast<int64_t>(mSumStartTime));
        // 分配本次运行代次, 用于抢占后旧go_task自检退出
        // mGoTaskActive 在 DoSummarize 入口置 true, 出口置 false, 供 Preempt 等待同步
        uint64_t generation = mRunGeneration.fetch_add(1, std::memory_order_release) + 1;
        SLOG_DEBUG << "SummaryTask: start, audioId=" << mAudioId << " generation=" << generation;
        auto self = Self();
        auto* goTask =
            WFTaskFactory::create_go_task("SummaryTask", [self, generation]() { self->DoSummarize(generation); });
        auto* series = Workflow::create_series_work(goTask, nullptr);
        series->start();
    }

    void SummaryTask::BuildInputFromDb() {
        auto &daoMg = MeetingDaoManager::GetInstance();
        auto audio = daoMg.GetByAudioId(mAccountId, mAudioId);

        mInput.mText = BuildTransContent(mAccountId, mAudioId);
        mInput.mDate = FormatRecordingTime(audio.mRecordingTime);
        mInput.mLocation = audio.mPlaces;
        mInput.mHost = audio.mModerator;
        mInput.mAttendees = audio.mAttendees;
        mInput.mMeetingKind = audio.mKind;
        mInput.mUseNote = audio.mUseNote;
        mInput.mReSummury = audio.mReSummury;
        mInput.mSumWordCount = audio.mSumWordCount;

        if (audio.mNoteId > 0) {
            NoteDao noteDao;
            auto note = noteDao.GetById(audio.mNoteId);
            mInput.mNote = note.mContent;
        }
    }

    void SummaryTask::Preempt() {
        SLOG_INFO << "SummaryTask: preempt, audioId=" << mAudioId;
        Stop();
        StopTimeoutTimer();
        StopProgressTimer();
        mLastProgress.store(0, std::memory_order_release);
        mExtraTimeAdded.store(false, std::memory_order_release);
        WaitGoTaskExit();
        TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::WaitSummary, AudioStatusMsg::WaitSummary);
        auto &daoMg = MeetingDaoManager::GetInstance();
        daoMg.UpdateSumProgress(mAccountId, mAudioId, 0, 0);  // 重置时间和进度
        daoMg.UpdateSumStartTime(mAccountId, mAudioId, 0);    // 重置开始时间
    }

    // 等待旧go_task退出: 限时等待mGoTaskActive变false, 超时则交由mRunGeneration保证静默退出
    // TIPS：无法保证一定能在规定时间内退出又不能影响其他任务饿死, 给予相对宽松的等待时间.
    // Stop()已通过 HmQwenInfer::Cancel() 触发取消, Summarize() 在下一个检查点抛 OperationCancelled,
    // 旧go_task捕获后退出.
    // 不持锁(避免与LmsHmModelManager::mMutex等形成锁顺序风险), 实测推理退出耗时<200ms
    void SummaryTask::WaitGoTaskExit() {
        constexpr int64_t kPreemptWaitMs = 1000;
        constexpr int kStepMs = 20;
        int64_t waited = 0;
        while (mGoTaskActive.load(std::memory_order_acquire) && waited < kPreemptWaitMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kStepMs));
            waited += kStepMs;
        }
        if (mGoTaskActive.load(std::memory_order_acquire)) {
            SLOG_WARN << "SummaryTask: preempt wait go_task timeout, audioId=" << mAudioId << " waitedMs=" << waited;
        } else {
            SLOG_INFO << "SummaryTask: preempt go_task exited, audioId=" << mAudioId << " waitedMs=" << waited;
        }
    }

    void SummaryTask::Stop() {
        // 置mRunning=false, 使DoSummarize中等待LMS就绪的循环能及时退出.
        // 不调SetComplete: 由go_task在DoSummarize中调NotifyResult(code=1)+SetComplete,
        // 使After()能根据code=1将音频状态置为WaitSummary.
        // (Cancel单独调SetComplete, go_task通过IsComplete()检测并静默退出)
        mRunning.store(false, std::memory_order_release);
        // HmQwenInfer::Cancel() 线程安全非阻塞: 运行中的 Summarize() 会在下一个检查点
        // 抛出 OperationCancelled, 由 DoSummarize 捕获并走取消流程.
        LmsHmModelManager::GetInstance().Cancel();
        SLOG_INFO << "SummaryTask: cancel requested, audioId=" << mAudioId;
    }

    void SummaryTask::Cancel() {
        BaseTask::Cancel();  // 设置取消标志并调用Stop()
        StopTimeoutTimer();
        StopProgressTimer();
        SetComplete();
    }

    // 等待LMS初始化就绪(LMS在main中异步初始化, 耗时可达分钟级).
    // 返回false表示任务已被取消/抢占或代次已过期, 调用方应静默退出.
    bool SummaryTask::WaitLmsReady(uint64_t generation) {
        while (!lms::IsLmsReady()) {
            // Cancel已置mComplete=true: 直接返回, 不重复完成
            if (IsComplete()) {
                SLOG_INFO << "SummaryTask: cancelled while waiting LMS ready, audioId=" << mAudioId;
                return false;
            }
            // 代次过期: 任务已被抢占并重新Start, 旧go_task必须静默退出
            if (mRunGeneration.load(std::memory_order_acquire) != generation) {
                SLOG_INFO << "SummaryTask: stale go_task while waiting LMS ready, audioId=" << mAudioId
                          << " gen=" << generation << " cur=" << mRunGeneration.load(std::memory_order_relaxed);
                return false;
            }
            // Preempt/Stop置mRunning=false: 静默退出, 不调NotifyResult.
            // 调度器已将本任务重新入队, 此处只需让旧go_task退出, 由下一轮Start()重新执行.
            if (!IsRunning()) {
                SLOG_INFO << "SummaryTask: preempted while waiting LMS ready, audioId=" << mAudioId;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return true;
    }

    void SummaryTask::DoSummarize(uint64_t generation) {
        // 标记go_task运行中, Preempt 会等待该标志变 false 后再让任务重新入队
        mGoTaskActive.store(true, std::memory_order_release);
        // RAII: 任何退出路径都清理 mGoTaskActive, 保证 Preempt 不会无限等待
        ScopeExit guard([this]() { mGoTaskActive.store(false, std::memory_order_release); });

        if (mInput.mText.empty()) {
            NotifyResult({Status {-1, std::string(SummaryMsg::InputTooShort)}, "", ""});
            return;
        }
        if (!WaitLmsReady(generation)) {
            return;
        }
        // 防御: LMS就绪期间若代次已过期, 直接静默退出
        if (mRunGeneration.load(std::memory_order_acquire) != generation) {
            SLOG_INFO << "SummaryTask: stale go_task after LMS ready, audioId=" << mAudioId << " gen=" << generation
                      << " cur=" << mRunGeneration.load(std::memory_order_relaxed);
            return;
        }
        TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::Summarying, AudioStatusMsg::Summarying);

        // HmQwenInfer::Summarize() 被 Cancel() 时会抛出 OperationCancelled;
        // 服务进程异常退出时也会抛 std::runtime_error. 必须在此捕获, 否则异常逃逸到 workflow 线程池
        // 的 __thrdpool_routine (不捕获 C++ 异常), 触发 std::terminate()->abort()->SIGABRT 使整个服务崩溃.
        qifeng::lms::SummaryResult result;
        bool cancelled = false;
        try {
            SLOG_DEBUG << "SummaryTask: summarize, audioId=" << mAudioId << " textLen=" << mInput.mText.length()
                       << " date=" << mInput.mDate << " location=" << mInput.mLocation << " host=" << mInput.mHost
                       << " text=" << mInput.mText;
            qifeng::lmshm::MettingHints hints;
            hints.topic = mHints.topic;
            hints.exampleSummary = mHints.exampleSummary;
            result = LmsHmModelManager::GetInstance().Summarize(BuildSummaryData(), hints, BuildProgressObserver());
        } catch (const qifeng::lmshm::OperationCancelled &) {
            cancelled = true;
        } catch (const std::exception &e) {
            // 非取消类异常(如 model server exited unexpectedly): 代次过期则静默退出, 否则按失败处理
            SLOG_WARN << "SummaryTask: summarize exception, audioId=" << mAudioId << " what=" << e.what();
            if (mRunGeneration.load(std::memory_order_acquire) != generation) {
                return;
            }
            NotifyResult({Status {-1, std::string(SummaryMsg::LmsConnectionError)}, "", ""});
            return;
        }
        // 迭代校验. 若被抢占且重新Start, mRunGeneration 已变化, 旧go_task静默退出.
        // 否则旧go_task会误用IsRunning()判定为新任务状态, 错误调用 HandleResult/NotifyResult 干扰新任务.
        if (mRunGeneration.load(std::memory_order_acquire) != generation) {
            SLOG_INFO << "SummaryTask: stale go_task after summarize, exit silently, audioId=" << mAudioId
                      << " gen=" << generation << " cur=" << mRunGeneration.load(std::memory_order_relaxed);
            return;
        }
        if (cancelled) {
            // Stop()(删除接口/Preempt)触发Cancel后, Summarize()抛 OperationCancelled.
            // 按 Stop() 注释设计: 由go_task调 NotifyResult(code=1)+SetComplete,
            // 使 After() 据code=1将音频状态置为 WaitSummary.
            SLOG_INFO << "SummaryTask: cancelled by user, audioId=" << mAudioId;
            NotifyResult({Status {1, std::string(SummaryMsg::ManualStopInfer)}, "", ""});
            // SetComplete();
            return;
        }
        if (!IsRunning()) {
            SLOG_INFO << "SummaryTask: preempted during summarize, audioId=" << mAudioId;
            return;
        }
        HandleResult(result);
    }

    std::function<void(qifeng::lms::ProgressStat)> SummaryTask::BuildProgressObserver() {
        auto self = Self();
        return [self](qifeng::lms::ProgressStat stat) {
            auto totalMs = stat.spentTime + stat.estimateRemainingTime;
            if (totalMs.count() <= 0) {
                return;
            }
            // 以引擎上报的总耗时校准进度推算, 使定时器进度与引擎口径一致
            self->mEstimatedTotalMs.store(static_cast<uint64_t>(totalMs.count()), std::memory_order_release);
            int progress = static_cast<int>(stat.spentTime.count() * 100 / totalMs.count());
            if (progress > 100) {
                progress = 100;
            }
            int64_t planFinish = static_cast<int64_t>(self->mSumStartTime + static_cast<uint64_t>(totalMs.count()));
            auto &daoMg = MeetingDaoManager::GetInstance();
            daoMg.UpdateSumProgress(self->mAccountId, self->mAudioId, progress, planFinish);
        };
    }

    void SummaryTask::StartProgressTimer() {
        if (mProgressTimerName.empty() || !IsRunning()) {
            return;
        }
        // 总结刚开始时立即执行一次进度推算, 不必等10s
        OnProgressTick();
        ScheduleNextProgress();
    }

    void SummaryTask::StopProgressTimer() {
        if (!mProgressTimerName.empty()) {
            TimerManager::GetInstance().CancelByName(mProgressTimerName);
        }
    }

    // 10s周期定时器: 每次tick后重新调度, IsRunning/IsComplete 失效则停止
    void SummaryTask::ScheduleNextProgress() {
        if (!IsRunning() || IsComplete()) {
            return;
        }
        auto self = Self();
        constexpr time_t kProgressIntervalSec = 10;
        auto* timerTask =
            WFTaskFactory::create_timer_task(mProgressTimerName, kProgressIntervalSec, 0L, [self](WFTimerTask* task) {
                if (task->get_state() != WFT_STATE_SUCCESS) {
                    SLOG_DEBUG << "SummaryTask: progress timer canceled, audioId=" << self->mAudioId;
                    return;
                }
                if (!self->IsRunning() || self->IsComplete()) {
                    return;
                }
                self->OnProgressTick();
                self->ScheduleNextProgress();
            });
        timerTask->start();
    }

    void SummaryTask::OnProgressTick() {
        uint64_t now = GetTimeMs();
        uint64_t spentMs = now - mSumStartTime;
        uint64_t totalMs = mEstimatedTotalMs.load(std::memory_order_acquire);

        // 若实际耗时已超过推算时长, 追加2-4分钟(demo取中值3分钟), 仅追加一次
        if (spentMs > totalMs && !mExtraTimeAdded.load(std::memory_order_acquire)) {
            constexpr uint64_t kExtraMs = 3ULL * 60ULL * 1000ULL;
            totalMs = spentMs + kExtraMs;
            mEstimatedTotalMs.store(totalMs, std::memory_order_release);
            // mExtraTimeAdded.store(true, std::memory_order_release);
            SLOG_INFO << "SummaryTask: overrun, add extra time, audioId=" << mAudioId << " spentMs=" << spentMs
                      << " newTotalMs=" << totalMs;
        }

        int progress = CalcProgress(spentMs, totalMs);
        int64_t planFinish = static_cast<int64_t>(mSumStartTime + totalMs);
        auto &daoMg = MeetingDaoManager::GetInstance();
        daoMg.UpdateSumProgress(mAccountId, mAudioId, progress, planFinish);
        SLOG_DEBUG << "SummaryTask: progress tick, audioId=" << mAudioId << " spentMs=" << spentMs
                   << " totalMs=" << totalMs << " progress=" << progress << " planFinish=" << planFinish;
    }

    // 字数<1000 → 5分钟; 1000-10000字 → 线性插值; 超过10000字 → 按相同斜率继续线性外推延长
    uint64_t SummaryTask::EstimateTotalMsByTextLen(size_t textLen) const {
        constexpr uint64_t kMinMs = 3ULL * 60ULL * 1000ULL;  // 1000字基准: 5分钟
        constexpr uint64_t kMaxMs = 7ULL * 60ULL * 1000ULL;  // 10000字基准: 14分钟
        constexpr size_t kMinLen = 1000;
        constexpr size_t kMaxLen = 10000;
        if (textLen < kMinLen) {
            return kMinMs;
        }
        if (textLen <= kMaxLen) {
            // 1000-10000字: 线性插值
            return kMinMs + (kMaxMs - kMinMs) * (textLen - kMinLen) / (kMaxLen - kMinLen);
        }
        // 超过10000字: 以10000字为起点按相同斜率继续延长
        return kMaxMs + (kMaxMs - kMinMs) * (textLen - kMaxLen) / (kMaxLen - kMinLen);
    }

    // 进度单调递增; 超过90%后维持不动, 不因推算延长而下降
    int SummaryTask::CalcProgress(uint64_t spentMs, uint64_t totalMs) {
        if (totalMs == 0) {
            return 0;
        }
        int raw = static_cast<int>(spentMs * 100 / totalMs);
        if (raw > 99) {
            raw = 99;  // 不推到100, 留给任务真正完成
        }
        int last = mLastProgress.load(std::memory_order_acquire);
        int next = raw;
        if (next < last) {
            next = last;  // 不因推算时长延长而下降
        } else if (last >= 90 && next < 99) {
            next = last;  // 已超过90%, 尽可能维持不动
        }
        if (next > last) {
            mLastProgress.store(next, std::memory_order_release);
        }
        return next;
    }

    // 构建HmQwenInfer::Summarize所需的会议信息(新HM接口直接使用该结构)
    qifeng::lmshm::MettingInfo SummaryTask::BuildSummaryData() {
        return qifeng::lmshm::MettingInfo(mInput.mDate, mInput.mLocation, mInput.mHost, mInput.mAttendees,
                                          mInput.mText);
    }

    void SummaryTask::HandleResult(const qifeng::lms::SummaryResult &result) {
        if (std::holds_alternative<qifeng::lms::SummaryData>(result)) {
            const auto &data = std::get<qifeng::lms::SummaryData>(result);
            SummaryResult sr;
            sr.mStatus = Status {};
            sr.mOverviewText = data.overview;
            sr.mKeywords = JoinKeywords(data.keywords);
            SLOG_INFO << "SummaryTask: success, audioId=" << mAudioId << " overviewLen=" << sr.mOverviewText.length();
            SLOG_DEBUG << "SummaryTask: success, audioId=" << mAudioId << " overview=" << sr.mOverviewText
                       << " keywords=" << sr.mKeywords;
            NotifyResult(sr);
            return;
        }
        const auto &err = std::get<qifeng::lms::SummaryError>(result);
        SLOG_WARN << "SummaryTask: failed, audioId=" << mAudioId << " errCode=" << static_cast<int>(err.code)
                  << " reason=" << err.reason;
        NotifyResult({MapErrorToStatus(err), "", ""});
    }

    void SummaryTask::NotifyResult(const SummaryResult &result) {
        mResult = result;
        // 被中断的不设置任务完成状态
        SLOG_INFO << "SummaryTask: notify result, audioId=" << mAudioId << " code=" << result.mStatus.GetCode()
                  << " status=" << result.mStatus.GetMsg();
        if (result.mStatus.GetCode() <= 0) {
            SetComplete();
        }

        if (mCallback) {
            mCallback(result);
        }
    }

    void SummaryTask::After() {
        StopProgressTimer();
        // 保存纪要到Summary表 + 推送WebSocket
        SummaryCallback::OnSummaryResult(mAudioId, mAccountId, mResult);
        PersistSumDuration();
        // 更新音频状态
        if (mResult.mStatus.IsSuccess()) {
            TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::SummaryComplete,
                                            AudioStatusMsg::SummaryComplete);
        } else if (mResult.mStatus.GetCode() == 1) {
            // 用户主动中断, 非错误
            TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::WaitSummary,
                                            AudioStatusMsg::WaitMeetingSummary);
        } else {
            // 失败原因动态传入(来自LMS的错误信息)
            TaskDbHelper::UpdateAudioStatus(mAccountId, mAudioId, AudioStatus::PermanentFailed,
                                            mResult.mStatus.GetMsg());
        }
    }

    void SummaryTask::PersistSumDuration() {
        // After在任务结束时调用一次, 此处统计完整耗时
        auto durationMs = GetTimeMs() - mSumStartTime;
        auto &daoMg = MeetingDaoManager::GetInstance();
        daoMg.UpdateSumDuration(mAccountId, mAudioId, static_cast<int64_t>(durationMs));
        SLOG_INFO << "SummaryTask: sum duration=" << durationMs << "ms, audioId=" << mAudioId;
    }

    void SummaryTask::StartTimeoutTimer() {
        if (mTimeoutTimerName.empty() || !IsRunning()) {
            return;
        }
        auto self = Self();

        int exTime = 0;
        if (!lms::IsLmsReady()) {
            exTime = 5 * 60;  // 额外多出5分钟
        }
        auto* timerTask = WFTaskFactory::create_timer_task(
            mTimeoutTimerName, static_cast<time_t>(ScheduleConfig::GetInstance().GetSummaryTimeoutSec() + exTime), 0L,
            [self](WFTimerTask* task) {
                if (task->get_state() != WFT_STATE_SUCCESS) {
                    SLOG_DEBUG << "SummaryTask: timeout timer canceled, audioId=" << self->mAudioId;
                    return;
                }
                self->OnTimeout();
            });
        timerTask->start();
        SLOG_INFO << "SummaryTask: timeout timer started, audioId=" << mAudioId
                  << " timeout=" << ScheduleConfig::GetInstance().GetSummaryTimeoutSec() + exTime << "s";
    }

    void SummaryTask::StopTimeoutTimer() {
        if (!mTimeoutTimerName.empty()) {
            TimerManager::GetInstance().CancelByName(mTimeoutTimerName);
        }
    }

    void SummaryTask::OnTimeout() {
        // 任务仍在运行则取消并设置失败
        if (!IsRunning()) {
            SLOG_INFO << "SummaryTask: timeout but task not running, audioId=" << mAudioId;
            return;
        }

        SLOG_WARN << "SummaryTask: timeout, canceling task, audioId=" << mAudioId;
        Stop();
        StopTimeoutTimer();
        StopProgressTimer();
        SetComplete();
        mResult.mStatus = Status {-1, std::string(SummaryMsg::SummaryTimeout)};

        if (mCallback) {
            mCallback({Status {-1, std::string(SummaryMsg::SummaryTimeout)}, "", ""});
        }
    }

}  // namespace qifeng_ca
