//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <string>

#include "qifeng_framework/common/logger.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"

#include "common/common.h"
#include "common/timer_manager.h"
#include "internal/display_manager.h"
#include "internal/display_refresh_task.h"
#include "internal/recording_manager.h"

namespace qifeng_ca {

    namespace {
        constexpr int RefreshIntervalSec = 1;
    }  // namespace

    DisplayRefreshTask &DisplayRefreshTask::GetInstance() {
        static DisplayRefreshTask Instance;
        return Instance;
    }

    void DisplayRefreshTask::Start() {
        if (mIsRunning.load(std::memory_order_acquire)) {
            SLOG_WARN << "DisplayRefreshTask already running";
            return;
        }
        mIsRunning.store(true, std::memory_order_release);
        SLOG_INFO << "DisplayRefreshTask started, interval=" << RefreshIntervalSec << "s";

        auto* firstTask =
            WFTaskFactory::create_go_task(WorkflowTakeName::DisplayRefreshTask.data(), [this]() { this->DoRefresh(); });
        auto* series = Workflow::create_series_work(firstTask, [this](const SeriesWork*) { this->ScheduleNext(); });
        series->start();
    }

    void DisplayRefreshTask::Stop() {
        mIsRunning.store(false, std::memory_order_release);
        SLOG_INFO << "DisplayRefreshTask stopped";
    }

    bool DisplayRefreshTask::IsRunning() const {
        return mIsRunning.load(std::memory_order_acquire);
    }

    void DisplayRefreshTask::ScheduleNext() {
        if (!mIsRunning.load(std::memory_order_acquire)) {
            return;
        }

        auto* timerTask = WFTaskFactory::create_timer_task(
            std::string(TimerName::DisplayRefreshTimer), static_cast<time_t>(RefreshIntervalSec), 0,
            [this](WFTimerTask* task) {
                if (task->get_state()) {
                    SLOG_ERROR << "DisplayRefreshTimer: state=" << task->get_state() << ", error=" << task->get_error();
                    return;
                }
                if (!mIsRunning.load(std::memory_order_acquire)) {
                    return;
                }
                auto* refreshTask = WFTaskFactory::create_go_task(WorkflowTakeName::DisplayRefreshTask.data(),
                                                                  [this]() { this->DoRefresh(); });
                auto* series =
                    Workflow::create_series_work(refreshTask, [this](const SeriesWork*) { this->ScheduleNext(); });
                series->start();
            });
        timerTask->start();
    }

    static void RefreshByPage(DisplayPage page) {
        auto &dispMgr = DisplayManager::GetInstance();
        auto &recMgr = RecordingManager::GetInstance();

        switch (page) {
            case DisplayPage::Idle:
                if (dispMgr.GetCurrentPopup() != 0) {
                    break;
                }
                dispMgr.RefreshIdlePage();
                dispMgr.RefreshDeviceInfo();
                break;
            case DisplayPage::Meeting:
                // 录制中(非暂停)由 AudioReadTask 驱动, 跳过避免重复
                if (recMgr.IsPaused() || !recMgr.IsRecording()) {
                    // dispMgr.RefreshMeetingPage();
                } else {
                    dispMgr.RefreshTime();
                }

                break;
            case DisplayPage::Fingerprint:
            default:
                // 指纹页由 FingerprintEnroller 驱动, 跳过
                break;
        }
    }

    void DisplayRefreshTask::DoRefresh() {
        DisplayPage page = DisplayManager::GetInstance().GetCurrentPage();
        RefreshByPage(page);
    }

}  // namespace qifeng_ca
