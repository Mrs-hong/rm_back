/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <cstdint>
#include <string>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/audio/audio_hal_utils.h"
#include "common/config/hal_config.h"
#include "common/service_readiness.h"
#include "common/status.h"
#include "internal/display_manager.h"
#include "internal/hal/fingerprint_bridge.h"
#include "internal/hal/hal_bridge.h"
#include "internal/hal/hal_types.h"
#include "internal/recording_manager.h"

namespace qifeng_ca {

    // 录音设备异常时, 在空闲页面显示麦克风异常弹窗(status=2)
    static void ShowMicErrorOnDisplay() {
        DisplayManager::GetInstance().ShowMicError();
    }

    bool StopFingerprintLed() {
        if (HalConfig::GetInstance().IsSkipButton()) {
            SLOG_WARN << "HalStopAudio: skip button enabled, skip led off";
        } else if (HalConfig::GetInstance().IsFingerprintBridge()) {
            FingerprintBridge::GetInstance().SetLed(qifeng::FingerprintLedState::Red, 500);
        }
        return true;
    }

    bool SetupFingerprintLed() {
        if (HalConfig::GetInstance().IsSkipButton()) {
            SLOG_WARN << "HalStartAudio: skip button enabled, skip led start";
        } else if (HalConfig::GetInstance().IsFingerprintBridge()) {
            if (FingerprintBridge::GetInstance().SetLed(qifeng::FingerprintLedState::Green, 500) !=
                FingerprintResult::OK) {
                SLOG_ERROR << "HalStartAudio: fingerprint led start failed";
                return false;
            }
        }
        return true;
    }

    // 录音LED设置: 绿灯亮(录音中), 支持指纹桥接LED和硬件LED
    static bool SetupRecordingLed() {
        auto &hal = HalBridge::GetInstance();
        LedState ledState {true, HalLedColor::Green};
        if (!SetupFingerprintLed()) {
            return false;
        }
        if (!hal.SetLedState(ledState)) {
            SLOG_ERROR << "HalStartAudio: led start failed";
        }
        return true;
    }

    static bool StopRecordingLed() {
        auto &hal = HalBridge::GetInstance();
        if (!StopFingerprintLed()) {
            return false;
        }

        LedState ledOff {true, HalLedColor::Red};
        hal.SetLedState(ledOff);
        return true;
    }

    Status HalStartAudio(const std::string &meetingName, const std::string &sponsor, AudioType type, bool isDisplay) {
        // 服务就绪检查: 防止HAL已启动但drogon未就绪时录音, 导致后台无法记录
        if (!ServiceReadiness::GetInstance().IsReady()) {
            SLOG_ERROR << "HalStartAudio: CA service not fully started, reject recording";
            return Status {-1, "服务尚未启动完成, 请稍后再试"};
        }
        auto &dispMgr = DisplayManager::GetInstance();
        if (dispMgr.IsFingerprint()) {
            return Status {-1, "指纹录入进行中, 无法开始会议"};
        }
        // 初始化HAL录音设备并启动
        auto &hal = HalBridge::GetInstance();
        if (!hal.InitRecorder()) {
            ShowMicErrorOnDisplay();
            return Status {-1, "录音设备初始化失败"};
        }
        if (hal.IsRecordingActive()) {
            return Status {-1, "设备已在录音"};
        }
        if (!hal.StartRecording()) {
            ShowMicErrorOnDisplay();
            StopFingerprintLed();  // 这里是为了解决麦克风不存在时LED变蓝不回退红色问题
            if (hal.IsRecordingUnavailable()) {
                return Status {-1, "录音设备未找到"};
            }
            if (hal.IsRecordingUninitialized()) {
                return Status {-1, "录音设备未初始化成功"};
            }
            return Status {-1, "启动录音失败"};
        }
        ScopeExit recordingFunc([&hal]() { hal.StopRecording(); });
        // 控制LED: 绿灯亮(录音中)
        if (!SetupRecordingLed()) {
            return Status {-1, "灯光启动失败"};
        }
        // 更新显示屏会议信息
        if (isDisplay) {
            MeetingMetrics metrics;
            metrics.mType = static_cast<uint8_t>(type);
            metrics.mStartTime = static_cast<int64_t>(GetTimeMs());
            metrics.mSponsor = sponsor;
            metrics.mMeetingName = meetingName;
            metrics.mAudioStatus = 2;
            metrics.mProgress = 0;
            metrics.mTotalTime = 0;
            dispMgr.UpdateMeetingFull(metrics);
            dispMgr.SwitchToMeeting();
        }
        recordingFunc.Release();
        return {};
    }

    void HalStopAudio() {
        auto &hal = HalBridge::GetInstance();
        // 未录音状态
        if (!hal.IsRecordingActive() && !RecordingManager::GetInstance().IsRecording()) {
            return;
        }
        // 停止HAL录音设备
        hal.StopRecording();

        // 控制LED: 灭灯
        StopRecordingLed();

        // 切换显示屏到待机页面
        DisplayManager::GetInstance().SwitchToIdle();
    }

    Status HalPauseAudio(RecordStopType type) {
        auto &hal = HalBridge::GetInstance();
        if (!hal.IsRecordingActive() && !RecordingManager::GetInstance().IsRecording()) {
            SLOG_WARN << "HalPauseAudio: audio not active, skip pause";
            return Status {-1, "录音设备未在录音"};
        }
        // 关闭HAL音频接收(StopRecording), 设备保持已初始化状态
        SLOG_DEBUG << "HalPauseAudio: pause audio type=" << static_cast<int>(type);
        hal.StopRecording();
        if (type != RecordStopType::Pause) {
            LedState ledOff {true, HalLedColor::Red};
            hal.SetLedState(ledOff);
        }

        // 显示屏: 保持会议页面, audioStatus改为1(暂停), 只刷新会议信息不刷新基础信息
        MeetingMetrics metrics;
        metrics.mMeetingName = RecordingManager::GetInstance().GetActiveMeetingName();
        metrics.mAudioStatus = 1;
        DisplayManager::GetInstance().UpdateMeetingInfo(metrics);

        SLOG_INFO << "HalPauseAudio: audio reception paused";
        return {};
    }

    Status HalResumeAudio(RecordStopType type) {
        auto &hal = HalBridge::GetInstance();
        if (hal.IsRecordingActive()) {
            return Status {-1, "录音设备已在录音"};
        }
        SLOG_DEBUG << "HalResumeAudio: resume audio type=" << static_cast<int>(type);
        if (!hal.StartRecording()) {
            if (hal.IsRecordingUnavailable()) {
                return Status {-1, "恢复录音失败, 请检测设备是否连接"};
            }
            return Status {-1, "恢复录音失败, 请检测设备"};
        }
        if (type != RecordStopType::Pause) {
            LedState ledState {true, HalLedColor::Green};
            hal.SetLedState(ledState);
        }

        // 显示屏: audioStatus改为2(录制中), 只刷新会议信息不刷新基础信息
        MeetingMetrics metrics;
        metrics.mMeetingName = RecordingManager::GetInstance().GetActiveMeetingName();
        metrics.mAudioStatus = 2;
        DisplayManager::GetInstance().UpdateMeetingInfo(metrics);

        SLOG_INFO << "HalResumeAudio: audio reception resumed";
        return {};
    }

}  // namespace qifeng_ca
