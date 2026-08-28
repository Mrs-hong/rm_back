//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <ctime>
#include <mutex>

#include "qifeng_framework/common/logger.h"

#include "common/common.h"
#include "internal/display_manager.h"
#include "internal/hal/hal_bridge.h"

namespace qifeng_ca {

    DisplayManager &DisplayManager::GetInstance() {
        static DisplayManager Instance;
        return Instance;
    }

    DisplayManager::DisplayManager() = default;

    void DisplayManager::Start() {
        SLOG_INFO << "DisplayManager started";
    }

    void DisplayManager::Stop() {
        SLOG_INFO << "DisplayManager stopped";
    }

    // ===== 页面切换 =====

    bool DisplayManager::SwitchToIdle() {
        std::lock_guard<std::mutex> lock(mPageMutex);
        mCurrentPage = DisplayPage::Idle;
        HalBridge::GetInstance().DisplaySwitchIdle();
        return true;
    }

    bool DisplayManager::IsFingerprint() {
        std::lock_guard<std::mutex> lock(mPageMutex);
        return mCurrentPage == DisplayPage::Fingerprint;
    }

    bool DisplayManager::SwitchToMeeting() {
        std::lock_guard<std::mutex> lock(mPageMutex);
        mCurrentPage = DisplayPage::Meeting;
        HalBridge::GetInstance().DisplaySwitchMeeting();
        return true;
    }

    bool DisplayManager::SwitchToFingerprint() {
        std::lock_guard<std::mutex> lock(mPageMutex);
        if (mCurrentPage == DisplayPage::Meeting) {
            SLOG_WARN << "DisplayManager: cannot switch to Fingerprint during Meeting";
            return false;
        }
        mCurrentPage = DisplayPage::Fingerprint;
        HalBridge::GetInstance().DisplaySwitchFingerprint();
        return true;
    }

    DisplayPage DisplayManager::GetCurrentPage() const {
        std::lock_guard<std::mutex> lock(mPageMutex);
        return mCurrentPage;
    }

    // ===== 全量页面更新 =====

    void DisplayManager::RefreshIdlePage() {
        DisplayPage page = GetCurrentPage();
        if (page != DisplayPage::Idle) {
            return;
        }
        auto metrics = mCollector.CollectIdleMetrics();
        HalBridge::GetInstance().DisplayUpdateIdle(metrics);
    }

    MeetingMetrics DisplayManager::CollectMeetingMetricsWithPopup() {
        uint16_t popupTip = mPopup.Get();
        return mCollector.CollectMeetingMetrics(popupTip);
    }

    void DisplayManager::RefreshMeetingPage() {
        DisplayPage page = GetCurrentPage();
        if (page != DisplayPage::Meeting) {
            return;
        }
        auto metrics = CollectMeetingMetricsWithPopup();
        HalBridge::GetInstance().DisplayUpdateMeetingInfoOnly(metrics);
    }

    void DisplayManager::RefreshDeviceInfo() {
        auto metrics = mCollector.CollectDeviceInfo();
        HalBridge::GetInstance().DisplayUpdateDeviceInfo(metrics);
    }

    void DisplayManager::RefreshTime() {
        auto metrics = CollectMeetingMetricsWithPopup();
        HalBridge::GetInstance().DisplaySetTime(metrics);
    }

    void DisplayManager::UpdateMeetingFull(const MeetingMetrics &m) {
        HalBridge::GetInstance().DisplayUpdateMeeting(m);
    }

    void DisplayManager::UpdateFingerprintPage(const FingerprintDisplayData &d) {
        HalBridge::GetInstance().DisplayUpdateFingerprint(d);
    }

    // ===== 部分更新 =====

    void DisplayManager::UpdateMeetingInfo(const MeetingMetrics &m) {
        if (GetCurrentPopup() != 0) {
            return;
        }
        HalBridge::GetInstance().DisplayUpdateMeetingInfoOnly(m);
    }

    void DisplayManager::UpdateWaveform(const std::vector<int32_t> &w) {
        HalBridge::GetInstance().DisplayUpdateWaveform(w);
    }

    void DisplayManager::UpdateIdleStatus(int32_t deviceStatus) {
        IdleMetrics metrics;
        metrics.mDeviceStatus = deviceStatus;
        HalBridge::GetInstance().DisplayUpdateIdle(metrics);
    }

    // ===== 弹窗统一管理 =====

    void DisplayManager::ShowPopup(uint16_t tip, uint64_t durationMs) {
        mPopup.Show(tip, durationMs);
    }

    uint16_t DisplayManager::GetCurrentPopup() {
        return mPopup.Get();
    }

    void DisplayManager::ClearPopup() {
        mPopup.Clear();
    }

    // ===== 复合场景入口 =====

    void DisplayManager::ShowBootDisplay() {
        SwitchToIdle();
        IdleMetrics idle;
        idle.mDeviceStatus = 1;
        idle.mTaskStatus = 1;
        HalBridge::GetInstance().DisplayUpdateIdle(idle);
        SLOG_INFO << "DisplayManager: boot display done";
    }

    void DisplayManager::ShowDiskError() {
        SwitchToIdle();
        ShowPopup(2, 2000);
        UpdateIdleStatus(2);
    }

    void DisplayManager::ShowMicError() {
        SwitchToIdle();
        ShowPopup(3, 2000);
        UpdateIdleStatus(3);
    }

    void DisplayManager::ShowMeetingPauseError() {
        SwitchToIdle();
        ShowPopup(4, 2000);
        UpdateIdleStatus(4);
    }

    void DisplayManager::ShowMeetingTimeOutError() {
        SwitchToIdle();
        ShowPopup(5, 2000);
        UpdateIdleStatus(5);
    }

    // ===== 升级场景入口 =====

    void DisplayManager::ShowUpgradeInProgress() {
        {
            std::lock_guard<std::mutex> lock(mPageMutex);
            mCurrentPage = DisplayPage::Upgrade;
        }
        HalBridge::GetInstance().DisplaySwitchUpgrade();
        UpgradeDisplayData data;
        data.mTitle = 1;    // 系统升级中
        data.mTypes = 1;    // 系统升级中
        data.mLoading = 0;  // 升级中动画
        HalBridge::GetInstance().DisplayUpdateUpgrade(data);
        SLOG_INFO << "DisplayManager: show upgrade in progress";
    }

    void DisplayManager::ShowUpgradeResult(bool success) {
        {
            std::lock_guard<std::mutex> lock(mPageMutex);
            mCurrentPage = DisplayPage::Upgrade;
        }
        HalBridge::GetInstance().DisplaySwitchUpgrade();
        UpgradeDisplayData data;
        if (success) {
            data.mTitle = 2;  // 成功
            data.mTypes = 2;  // 成功
        } else {
            data.mTitle = 3;  // 失败
            data.mTypes = 3;  // 失败
        }
        data.mLoading = 1;  // 完成
        HalBridge::GetInstance().DisplayUpdateUpgrade(data);
        SLOG_INFO << "DisplayManager: show upgrade result, success=" << success;
    }

}  // namespace qifeng_ca
