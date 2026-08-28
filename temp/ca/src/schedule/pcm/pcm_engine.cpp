//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "schedule/pcm/pcm_engine.h"

#include "qifeng_framework/common/logger.h"
#include "schedule/pcm/task_recoverer.h"
#include "schedule/pcm/task_scheduler.h"
#include <memory>

namespace qifeng_ca {

    PcmEngine &PcmEngine::GetInstance() {
        static PcmEngine Instance;
        return Instance;
    }

    PcmEngine::~PcmEngine() {
        Stop();
    }

    bool PcmEngine::Init() {
        SLOG_INFO << "PcmEngine: initializing";
        mTaskScheduler = std::make_shared<TaskScheduler>();

        // 从DB恢复未完成任务, 提交到TaskScheduler
        TaskRecoverer recoverer;
        auto tasks = recoverer.RecoverFromMeetingDb();
        for (const auto &task : tasks) {
            mTaskScheduler->Submit(task);
        }

        SLOG_INFO << "PcmEngine: initialized, recovered " << tasks.size() << " tasks";
        return true;
    }

    void PcmEngine::Submit(TaskPtr task) {
        mTaskScheduler->Submit(task);
    }

    bool PcmEngine::Run() {
        return mTaskScheduler->Run();
    }

    void PcmEngine::Stop() {
        mTaskScheduler->Stop();
        SLOG_INFO << "PcmEngine: stopped";
    }

    TaskPtr PcmEngine::TopTask() const {
        return mTaskScheduler->TopTask();
    }

    TaskPtr PcmEngine::CurrentRunningTask() const {
        return mTaskScheduler->CurrentRunningTask();
    }

    // ---- 查询接口(委托TaskScheduler) ----

    bool PcmEngine::IsRunning(const std::string &audioId) const {
        return mTaskScheduler->IsRunning(audioId);
    }

    bool PcmEngine::IsInOfflineTrans(const std::string &audioId) const {
        return mTaskScheduler->IsInOfflineTrans(audioId);
    }

    bool PcmEngine::IsInSummary(const std::string &audioId) const {
        return mTaskScheduler->IsInSummary(audioId);
    }

    int PcmEngine::StopSummaryTask(const std::string &audioId) {
        return mTaskScheduler->StopSummaryTask(audioId);
    }

    void PcmEngine::CancelTaskByAudioId(const std::string &audioId) {
        mTaskScheduler->CancelByAudioId(audioId);
    }

}  // namespace qifeng_ca
