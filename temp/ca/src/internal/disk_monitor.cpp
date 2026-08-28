//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <sys/statvfs.h>

#include "qifeng_framework/common/logger.h"

#include "common/audio/audio_utils.h"
#include "common/config/hal_config.h"
#include "common/ws/system_message_notifier.h"
#include "core/config/config_service.h"
#include "internal/disk_monitor.h"
#include "internal/hal/hal_bridge.h"
#include "internal/hal/hal_types.h"
#include "internal/recording_manager.h"

namespace qifeng_ca {

    // 磁盘可用低于 1% 视为硬限制
    static constexpr int DiskLowAvailPercent = 1;
    // 磁盘可用低于 1GB 视为硬限制
    static constexpr uint64_t DiskMinAvailBytes = 1024ULL * 1024ULL * 1024ULL;  // 1GB

    DiskMonitor &DiskMonitor::GetInstance() {
        static DiskMonitor Instance;
        return Instance;
    }

    DiskAlertLevel DiskMonitor::CheckAndUpdateLed() {
        auto info = DiskUtils::GetDiskSpace("/");
        DiskAlertLevel level = CalcAlertLevel(info);

        if (level != mCurrentLevel) {
            UpdateLedAlert(level);
            mCurrentLevel = level;
        }
        return level;
    }

    bool DiskMonitor::CanStartRecording(const std::string &path) {
        auto info = DiskUtils::GetDiskSpace(path);
        DiskAlertLevel level = CalcAlertLevel(info);
        // 硬限制时不允许开始录音
        return level != DiskAlertLevel::HardLimit;
    }

    int32_t DiskMonitor::GetAvailableMinutes(const std::string &path) {
        auto info = DiskUtils::GetDiskSpace(path);
        int64_t bitrate = GetRecordingBitrate();
        if (bitrate <= 0) {
            return 0;
        }
        int64_t availSeconds = static_cast<int64_t>(info.mAvailBytes) / bitrate;
        return static_cast<int32_t>(availSeconds / 60);
    }

    int32_t DiskMonitor::GetTotalMinutes(const std::string &path) {
        auto info = DiskUtils::GetDiskSpace(path);
        int64_t bitrate = GetRecordingBitrate();
        if (bitrate <= 0) {
            return 0;
        }
        int64_t totalSeconds = static_cast<int64_t>(info.mTotalBytes) / bitrate;
        return static_cast<int32_t>(totalSeconds / 60);
    }

    // 获取数据库中的磁盘配置(软限制告警阈值/硬限制阈值)
    static void LoadDiskConfig(int &warningPercent, int &limitPercent) {
        ConfigService configSvc;
        GetSysBaseConfigResponse cfg;
        configSvc.GetSysBaseConfig(&cfg);
        warningPercent = cfg.disk_warning();
        limitPercent = cfg.disk_limit();
    }

    DiskAlertLevel DiskMonitor::CalcAlertLevel(const DiskSpaceInfo &info) {
        // 硬限制: 使用率 >= (100 - 1)% 或 剩余空间 < 1GB
        if (info.mUsagePercent >= (100 - DiskLowAvailPercent) || info.mAvailBytes < DiskMinAvailBytes) {
            SLOG_WARN << "DiskMonitor: hard limit, usage=" << info.mUsagePercent
                      << "%, avail=" << (info.mAvailBytes / (1024ULL * 1024)) << "MB";
            return DiskAlertLevel::HardLimit;
        }

        int warningPercent = 0;
        int limitPercent = 0;
        LoadDiskConfig(warningPercent, limitPercent);

        // 数据库配置的硬限制
        if (limitPercent > 0 && info.mUsagePercent >= limitPercent) {
            SLOG_WARN << "DiskMonitor: hard limit by config, usage=" << info.mUsagePercent
                      << "%, limit=" << limitPercent << "%";
            return DiskAlertLevel::HardLimit;
        }
        // 数据库配置的软限制告警
        if (warningPercent > 0 && info.mUsagePercent >= warningPercent) {
            SLOG_WARN << "DiskMonitor: soft limit, usage=" << info.mUsagePercent << "%, warning=" << warningPercent
                      << "%";
            return DiskAlertLevel::SoftLimit;
        }
        return DiskAlertLevel::Normal;
    }

    void DiskMonitor::UpdateLedAlert(DiskAlertLevel level) {
        auto &hal = HalBridge::GetInstance();

        switch (level) {
            case DiskAlertLevel::HardLimit: {
                // 快闪红灯: 磁盘严重不足
                // LedState state {true, HalLedColor::Red, HalLedMode::BlinkFast};
                // hal.SetLedState(state);
                SLOG_WARN << "DiskMonitor: disk hard limit, led=blink_fast_red";
                break;
            }
            case DiskAlertLevel::SoftLimit: {
                // 慢闪红灯: 磁盘告警, 并推送告警通知
                // LedState state {true, HalLedColor::Red, HalLedMode::BlinkSlow};
                // hal.SetLedState(state);
                SystemMessageNotifier::GetInstance().SendDiskWarning();
                SLOG_WARN << "DiskMonitor: disk soft limit, led=blink_slow_red";
                break;
            }
            case DiskAlertLevel::Normal:
            default: {
                // 录音中保持绿灯, 否则红灯
                auto &recMgr = RecordingManager::GetInstance();
                if (recMgr.IsRecording()) {
                    LedState state {true, HalLedColor::Green, HalLedMode::Solid};
                    hal.SetLedState(state);
                } else {
                    LedState state {true, HalLedColor::Red, HalLedMode::Solid};
                    hal.SetLedState(state);
                }
                SLOG_INFO << "DiskMonitor: disk normal, led restored";
                break;
            }
        }
    }

    int64_t DiskMonitor::GetRecordingBitrate() const {
        // 获取麦克风采样率、声道数、位深并推断时长
        AudioUtilsConfig audioCfg = HalBridge::GetInstance().GetAudioFormat();
        return static_cast<int64_t>(AudioUtils::CalculateOneSecondBytes(audioCfg));
    }

}  // namespace qifeng_ca
