//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "common/audio_enums.h"
#include "common/common.h"
#include "common/config/device_config.h"
#include "common/config/meeting_config.h"
#include "common/utils/disk_utils.h"
#include "core/dashboard/dashboard_service.h"
#include "core/network/network_service.h"
#include "core/network/wifi_service.h"
#include "dao_managers/meeting_dao_manager.h"
#include "dao_managers/user_dao_manager.h"
#include "internal/hal/display/display_collector.h"
#include "internal/recording_manager.h"
#include "schedule/pcm/pcm_engine.h"

namespace qifeng_ca {

    // ===================== 静态辅助函数 =====================

    static void SetMeetingProgress(MeetingMetrics &metrics, const std::string &audioId) {
        auto audio = MeetingDaoManager::GetInstance().GetByAudioIdGlobal(audioId);
        int64_t elapsedSec = audio.mTotalTime / 1000;
        if (elapsedSec < 0) {
            elapsedSec = 0;
        }
        metrics.mTotalTime = static_cast<int32_t>(elapsedSec);

        int64_t meetingMaxDurationSec = MeetingConfig::GetInstance().GetLimitRecordTimeSec();
        int32_t progress = static_cast<int32_t>(elapsedSec * 100LL / meetingMaxDurationSec);
        if (progress < 0) {
            progress = 0;
        } else if (progress > 100) {
            progress = 100;
        }
        metrics.mProgress = progress;
    }

    [[maybe_unused]] static const char* GetTaskStatusPrefix(AudioStatus status) {
        switch (status) {
            case AudioStatus::WaitTrans:
                return "会议待转写";
            case AudioStatus::Transing:
                return "AI转写中";
            case AudioStatus::WaitSummary:
                return "会议待总结";
            case AudioStatus::Summarying:
                return "AI总结中";
            default:
                return "暂无待处理会议";
        }
    }

    static std::string FormatTaskStatusText(const models::Audio &audio) {
        // auto status = static_cast<AudioStatus>(audio.mStatus);
        // const char* prefix = GetTaskStatusPrefix(status);
        int64_t planFinishTime = audio.mPlanFinishTime;
        if (planFinishTime <= 0) {
            return "";
            // return std::string(prefix);
        }
        time_t finishSec = static_cast<time_t>(planFinishTime / 1000);
        struct tm* tm = std::localtime(&finishSec);
        if (!tm) {
            return "";
            // return std::string(prefix);
        }
        std::array<char, 64> buf {};
        // std::snprintf(buf.data(), buf.size(), "%s,预计%02d:%02d完成", prefix, tm->tm_hour, tm->tm_min);
        std::snprintf(buf.data(), buf.size(), "%02d:%02d", tm->tm_hour, tm->tm_min);
        return std::string(buf.data());
    }

    static void SetTaskStatusByAudio(IdleMetrics &metrics, const models::Audio &audio) {
        switch (static_cast<AudioStatus>(audio.mStatus)) {
            case AudioStatus::WaitTrans:
                metrics.mTaskStatus = 2;
                break;
            case AudioStatus::Transing:
                metrics.mTaskStatus = 3;
                break;
            case AudioStatus::WaitSummary:
                metrics.mTaskStatus = 4;
                break;
            case AudioStatus::Summarying:
                metrics.mTaskStatus = 5;
                break;
            default:
                metrics.mTaskStatus = 1;
                break;
        }
    }

    static int32_t CalcTaskProgressLevel(const models::Audio &audio) {
        int progress = 0;
        auto status = static_cast<AudioStatus>(audio.mStatus);
        if (status == AudioStatus::WaitTrans || status == AudioStatus::Transing) {
            progress = audio.mTransLastProgress;
        } else if (status == AudioStatus::WaitSummary || status == AudioStatus::Summarying) {
            progress = audio.mSumLastProgress;
        }
        int level = progress / 12 + 1;
        if (level < 1) {
            level = 1;
        } else if (level > 12) {
            level = 12;
        }
        return level;
    }

    static int64_t GetTaskStartTime(const models::Audio &audio) {
        auto status = static_cast<AudioStatus>(audio.mStatus);
        if (status == AudioStatus::WaitTrans || status == AudioStatus::Transing) {
            return audio.mTransStartTime;
        }
        if (status == AudioStatus::WaitSummary || status == AudioStatus::Summarying) {
            return audio.mSumStartTime;
        }
        return 0;
    }

    static int64_t CalcElapsedMinutes(int64_t startTime) {
        if (startTime <= 0) {
            return 0;
        }
        int64_t elapsedMs = static_cast<int64_t>(GetTimeMs()) - startTime;
        if (elapsedMs < 0) {
            return 0;
        }
        return elapsedMs / (1000LL * 60);
    }

    static int64_t CalcTotalMinutes(int64_t startTime, int64_t planFinishTime) {
        if (startTime <= 0 || planFinishTime <= startTime) {
            return 0;
        }
        return (planFinishTime - startTime) / (1000LL * 60);
    }

    static void SetTaskProgressInfo(IdleMetrics &metrics, const models::Audio &audio) {
        metrics.mTaskProportion = CalcTaskProgressLevel(audio);
        int64_t startTime = GetTaskStartTime(audio);
        int64_t elapsedMinutes = CalcElapsedMinutes(startTime);
        int64_t totalMinutes = CalcTotalMinutes(startTime, audio.mPlanFinishTime);
        metrics.mTaskProgressText = std::to_string(elapsedMinutes);
        metrics.mTaskTotalTimeText = std::to_string(totalMinutes);
        metrics.mTaskStatusText = FormatTaskStatusText(audio);
    }

    // ===================== DisplayCollector 实现 =====================

    IdleMetrics DisplayCollector::CollectIdleMetrics() {
        IdleMetrics metrics;

        auto diskInfo = DashboardService::CollectDiskTimeInfo();
        metrics.mAvailableTime = diskInfo.availableHours;
        int32_t ratioLevel = 0;
        if (diskInfo.ratioPercent == 0) {
            ratioLevel = 12;
        } else if (diskInfo.ratioPercent == 100) {
            ratioLevel = 1;
        } else {
            ratioLevel = 11 - ((diskInfo.ratioPercent - 1) / 10);
        }

        metrics.mAvailableTimeProp = ratioLevel;
        metrics.mAvailableTimeRatio = diskInfo.ratioPercent;
        SLOG_DEBUG << "DisplayCollector: available time ratio=" << metrics.mAvailableTimeRatio
                   << ", level=" << metrics.mAvailableTimeProp;

        CollectSummaryStats(metrics);
        CollectTaskStatus(metrics);
        metrics.mDeviceStatus = 1;
        return metrics;
    }

    MeetingMetrics DisplayCollector::CollectMeetingMetrics(uint16_t popupTip) {
        MeetingMetrics metrics;

        auto &recMgr = RecordingManager::GetInstance();
        if (!recMgr.IsRecording()) {
            return metrics;
        }

        metrics.mMeetingName = recMgr.GetActiveMeetingName();
        metrics.mStartTime = recMgr.GetActiveStartTime();
        uint64_t activeAccountId = recMgr.GetActiveAccountId();

        // uint64_t startTimeMs = static_cast<uint64_t>(recMgr.GetActiveStartTime()) * 1000ULL;
        // uint64_t cardEndTime = startTimeMs + 3 * 1000ULL;
        // if (GetTimeMs() < cardEndTime) {
        //     uint64_t guestId = GetGuestAccountId();
        //     metrics.mType = (activeAccountId == guestId) ? 2 : 3;
        // } else {
        metrics.mType = 1;
        // }

        auto user = UserDaoManager::GetInstance().GetByAccountId(activeAccountId);
        metrics.mSponsor = user.mUserName;
        // 暂停=1, 录制中=2, 连续无声10s的无音频输入=3
        if (recMgr.IsPaused()) {
            metrics.mAudioStatus = 1;
        } else if (recMgr.IsNoAudioInput()) {
            metrics.mAudioStatus = 3;
        } else {
            metrics.mAudioStatus = 2;
        }
        metrics.mOperationTip = (popupTip != 0) ? popupTip : 1;

        SetMeetingProgress(metrics, recMgr.GetActiveAudioId());
        return metrics;
    }

    DeviceInfoMetrics DisplayCollector::CollectDeviceInfo() {
        DeviceInfoMetrics metrics;

        // 优先通过 WifiService 获取 WiFi 信息(IP + SSID)
        WifiService wifiService;
        GetWifiStatusResponse wifiResp;
        if (wifiService.GetWifiStatus(wifiResp).IsSuccess()) {
            metrics.mWifiSSID = wifiResp.ssid();
            // SSID 与 IP 均非空时视为 WiFi 可用, 直接使用 WiFi IP
            if (!wifiResp.ssid().empty() && !wifiResp.ip_address().empty()) {
                metrics.mIpAddress = wifiResp.ip_address();
                metrics.mWifiConnected = true;
                return metrics;
            }
        }

        // WiFi 不可用或 SSID/IP 任一为空, 回退到有线 IP
        NetworkService netService;
        GetNetworkResponse resp;
        if (netService.GetNetworkInfo(&resp).IsSuccess()) {
            metrics.mIpAddress = resp.ip();
        }
        metrics.mWifiConnected = false;
        return metrics;
    }

    void DisplayCollector::CollectSummaryStats(IdleMetrics &metrics) {
        DashboardService svc;
        SummaryStatsResponse resp;
        if (svc.GetSummaryStats(&resp).GetCode() != 0) {
            SLOG_WARN << "DisplayCollector: get summary stats failed";
            return;
        }
        metrics.mSummaryDone = resp.done();
        metrics.mSummaryWait = resp.waiting();
        metrics.mSummaryTotal = resp.total();
        if (resp.total() == 0) {
            metrics.mSummaryRatio = 22;
        } else {
            int32_t level = 21 - resp.ratio() / 5;
            if (level < 1) {
                level = 1;
            } else if (level > 21) {
                level = 21;
            }
            metrics.mSummaryRatio = level;
        }
    }

    void DisplayCollector::CollectTaskStatus(IdleMetrics &metrics) {
        auto noTask = [](IdleMetrics &inMetrics) {
            inMetrics.mTaskStatus = 1;
            inMetrics.mTaskMeetingName = "";
            inMetrics.mTaskStatusText = "";
            inMetrics.mTaskProgressText = "";
            inMetrics.mTaskTotalTimeText = "";
            inMetrics.mTaskProportion = 0;
        };

        auto task = PcmEngine::GetInstance().CurrentRunningTask();
        if (!task) {
            noTask(metrics);
            return;
        }
        auto audio = MeetingDaoManager::GetInstance().GetByAudioIdGlobal(task->GetAudioId());
        if (audio.mTheme.empty()) {
            SLOG_WARN << "DisplayCollector: audio theme is empty, audioId=" << task->GetAudioId();
            noTask(metrics);
            return;
        }
        metrics.mTaskMeetingName = audio.mTheme;
        SetTaskStatusByAudio(metrics, audio);
        SetTaskProgressInfo(metrics, audio);
    }

}  // namespace qifeng_ca
