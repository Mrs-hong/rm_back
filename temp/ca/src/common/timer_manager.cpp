//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "common/timer_manager.h"

#include "qifeng_framework/common/logger.h"
#include "workflow/WFTaskFactory.h"

namespace qifeng_ca {

    void TimerManager::DefaultRegisterAll() {
        std::lock_guard<std::mutex> lock(mTimerMutex);
        RegisterLocked(TimerName::DeviceCollectTimer);
        RegisterLocked(TimerName::AudioReadTimer);
        RegisterLocked(TimerName::DisplayRefreshTimer);
        RegisterLocked(TimerName::FingerprintPageSwitchTimer);
        RegisterLocked(TimerName::FingerprintEnrollTimeoutTimer);
        RegisterLocked(TimerName::OtaDetectTimer);
    }

    void TimerManager::Register(std::string_view timerName) {
        std::lock_guard<std::mutex> lock(mTimerMutex);
        RegisterLocked(timerName);
    }

    void TimerManager::RegisterLocked(std::string_view timerName) {
        std::string name(timerName);
        for (const auto &registered : mRegisteredTimers) {
            if (registered == name) {
                return;
            }
        }
        mRegisteredTimers.push_back(std::move(name));
        SLOG_INFO << "TimerManager: registered timer=" << timerName;
    }

    void TimerManager::CancelAll() {
        std::lock_guard<std::mutex> lock(mTimerMutex);
        SLOG_INFO << "TimerManager: canceling all timers, count=" << mRegisteredTimers.size();
        for (const auto &name : mRegisteredTimers) {
            int ret = WFTaskFactory::cancel_by_name(name);
            if (ret != 0) {
                SLOG_WARN << "TimerManager: cancel timer=" << name << " ret=" << ret;
            } else {
                SLOG_INFO << "TimerManager: canceled timer=" << name;
            }
        }
        SLOG_INFO << "TimerManager: all timers canceled";
    }

    void TimerManager::CancelByName(std::string_view timerName) {
        std::lock_guard<std::mutex> lock(mTimerMutex);
        std::string name(timerName);
        int ret = WFTaskFactory::cancel_by_name(name);
        if (ret != 0) {
            SLOG_WARN << "TimerManager: cancel timer=" << name << " ret=" << ret;
        } else {
            SLOG_INFO << "TimerManager: canceled timer=" << name;
        }
    }

}  // namespace qifeng_ca
