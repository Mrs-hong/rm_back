//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_MANAGER_H
#define QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_MANAGER_H

#include <cstdint>
#include <vector>

#include "internal/hal/display/display_collector.h"
#include "internal/hal/display/display_page.h"
#include "internal/hal/display/popup_manager.h"
#include "internal/hal/hal_types.h"

namespace qifeng_ca {

    // 显示屏统一入口, 只提供显示方法
    class DisplayManager {
    public:
        static DisplayManager &GetInstance();

        // ===== 生命周期 =====
        void Start();
        void Stop();

        // ===== 页面切换（带状态互斥）=====
        bool SwitchToIdle();         // 总是允许(复位态)
        bool SwitchToMeeting();      // Fingerprint 态拒绝
        bool SwitchToFingerprint();  // Meeting 态拒绝
        DisplayPage GetCurrentPage() const;

        bool IsFingerprint();  // 是否支持录入

        // ===== 全量页面更新（常用方式）=====
        void RefreshIdlePage();                           // 采集+下发空闲页全部数据
        void RefreshMeetingPage();                        // 采集+下发会议页信息(不含basic)
        void RefreshDeviceInfo();                         // 采集+下发设备信息
        void RefreshTime();                               // 采集系统时间+下发
        void UpdateMeetingFull(const MeetingMetrics &m);  // 会议开始: basic+info一次性下发
        void UpdateFingerprintPage(const FingerprintDisplayData &d);

        // ===== 部分更新（精细控制）=====
        void UpdateMeetingInfo(const MeetingMetrics &m);  // 仅info(暂停/恢复)
        void UpdateWaveform(const std::vector<int32_t> &w);
        void UpdateIdleStatus(int32_t deviceStatus);

        // ===== 弹窗统一管理 =====
        void ShowPopup(uint16_t tip, uint64_t durationMs);
        uint16_t GetCurrentPopup();
        void ClearPopup();

        // ===== 复合场景入口 =====
        void ShowBootDisplay();
        void ShowMicError();
        void ShowDiskError();
        void ShowMeetingPauseError();
        void ShowMeetingTimeOutError();

        // ===== 升级场景入口 =====
        // 显示"系统升级中"页面(切到Upgrade页 + 设置升级中状态)
        void ShowUpgradeInProgress();
        // 显示升级结果(切到Upgrade页 + 设置成功/失败状态)
        void ShowUpgradeResult(bool success);

    private:
        DisplayManager();

        MeetingMetrics CollectMeetingMetricsWithPopup();

    private:
        DisplayCollector mCollector;
        PopupManager mPopup;
        mutable std::mutex mPageMutex;
        DisplayPage mCurrentPage {DisplayPage::Idle};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_MANAGER_H
