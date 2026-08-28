//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_HAL_HAL_BRIDGE_H
#define QIFENG_CA_INCLUDE_INTERNAL_HAL_HAL_BRIDGE_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "qifeng_framework/hal/led/led_hal.h"

#include "common/audio/audio_utils.h"
#include "internal/hal/hal_bridge.h"
#include "internal/hal/hal_types.h"

namespace qifeng {
    class ButtonHAL;
    class LedHAL;
    class RecordHAL;
    class ScreenHAL;
    enum class ButtonEventType : uint8_t;
    enum class LedColor : uint8_t;
    enum class AudioState : uint8_t;
}  // namespace qifeng

namespace qifeng_ca {

    // 按键事件回调类型
    using ButtonCallback = std::function<void(const ButtonEventData &)>;

    // 麦克风静音按键事件枚举
    enum class MicMuteEvent : uint8_t { Mute = 0, Unmute = 1 };

    // LED事件回调类型: ledId + 事件类型(Mute/Unmute)
    using LedEventCallback = std::function<void(uint32_t ledId, MicMuteEvent event)>;

    // HAL桥接层: 统一管理所有HAL设备的初始化/释放/操作
    class HalBridge {
    public:
        static HalBridge &GetInstance();

        // ---- 生命周期 ----
        bool InitAll();
        void ReleaseAll();

        // ---- 按键 ----
        bool InitButton();
        void SetButtonCallback(ButtonCallback callback);

        // ---- LED ----
        bool InitLed();
        bool SetLedState(const LedState &state);
        // 注册灯光按键(麦克风静音/取消静音)事件回调
        void SetLedCallback(LedEventCallback callback);

        // ---- 录音 ----
        bool InitRecorder();
        bool StartRecording();
        bool StopRecording();
        size_t ReadAudio(std::vector<uint8_t> &outBuffer, size_t maxSize);
        bool IsRecordingActive() const;
        bool IsRecordingUninitialized() const;
        bool IsRecordingUnavailable() const;
        bool IsRecordingIdle() const;

        /**
         * @brief 查询麦克风是否已连接
         * @return 录音设备已初始化且状态非 Unavailable 时返回 true
         */
        bool HasMicrophone() const;

        // ---- 录音采样率 ----
        // 如果接口获取失败则使用默认的值或者上次的值
        AudioUtilsConfig GetAudioFormat();

        // ---- 显示屏 ----
        bool InitDisplay();

        // ---- 指纹 ----
        bool InitFingerprint();

        // ====== 显示屏页面切换 ======
        // 切换到空闲/待机页面
        void DisplaySwitchIdle();
        // 切换到会议页面
        void DisplaySwitchMeeting();
        // 切换到指纹录入页面
        void DisplaySwitchFingerprint();

        // ====== 显示屏数据更新 ======
        // 更新空闲页面数据(磁盘时长/纪要统计/任务状态)
        void DisplayUpdateIdle(const IdleMetrics &metrics);
        // 更新会议页面数据(类型/名称/操作提示/音频状态/进度/发起人/开始时间)
        void DisplayUpdateMeeting(const MeetingMetrics &metrics);
        // 仅更新会议页面信息(类型/名称/操作提示/音频状态/进度), 不刷新发起人/开始时间
        void DisplayUpdateMeetingInfoOnly(const MeetingMetrics &metrics);
        // 更新会议提示 - 无权操作
        void DisplayAccessibleUpdateMeeting();
        // 更新会议页面波形数据(实时音频能量)
        void DisplayUpdateWaveform(const std::vector<int32_t> &waveform);
        // 更新设备信息(WiFi状态/SSID/IP地址)
        void DisplayUpdateDeviceInfo(const DeviceInfoMetrics &metrics);
        // 更新指纹录入页面数据(进度1~7/提示/结果)
        void DisplayUpdateFingerprint(const FingerprintDisplayData &data);
        // 同步时间到显示屏
        void DisplaySetTime(const MeetingMetrics &metrics);

        // ====== 升级页面 ======
        // 切换到升级页面
        void DisplaySwitchUpgrade();
        // 更新升级页面数据(标题/类型/loading动画)
        void DisplayUpdateUpgrade(const UpgradeDisplayData &data);

        // ====== 屏幕固件升级 ======
        // 通过串口升级屏幕固件, filePaths为固件文件路径列表
        // 返回值: 0=成功, -1=未初始化, -2=升级进行中, -3=升级失败
        int UpgradeScreenFirmware(const std::vector<std::string> &filePaths);

    private:
        HalBridge() = default;

        static void OnButtonEvent(uint32_t buttonId, qifeng::ButtonEventType event);
        static void OnLedEvent(uint32_t ledId, qifeng::LedEventType eventType);
        qifeng::LedColor ConvertLedColor(HalLedColor color) const;
        qifeng::LedMode ConvertLedMode(HalLedMode mode) const;

        // ====== Display辅助: 转换Metrics为ScreenHAL数据类型并下发 ======
        // 空闲页: 可用时长占比
        void UpdateIdleAvailability(const IdleMetrics &metrics);
        // 空闲页: 纪要统计(完成/待处理/总数)
        void UpdateIdleSummary(const IdleMetrics &metrics);
        // 空闲页: 当前任务信息(名称/进度/状态)
        void UpdateIdleTask(const IdleMetrics &metrics);
        // 空闲页: 设备状态
        void UpdateIdleStatus(const IdleMetrics &metrics);
        // 会议页: 会议信息(类型/名称/操作提示/音频状态/进度)
        void UpdateMeetingInfo(const MeetingMetrics &metrics);
        // 会议页: 基础信息(发起人/开始时间)
        void UpdateMeetingBasic(const MeetingMetrics &metrics);
        // 会议页: 实时音频能量波形
        void UpdateMeetingEnergy(const std::vector<int32_t> &waveform);
        // 设备信息: WiFi连接状态/SSID/IP地址
        void UpdateDeviceInfo(const DeviceInfoMetrics &metrics);
        // 指纹录入: 进度/提示/结果
        void UpdateFingerprint(const FingerprintDisplayData &data);
        // 升级页: 转换并下发UpgradeData
        void UpdateUpgrade(const UpgradeDisplayData &data);

        // HAL设备实例
        std::unique_ptr<qifeng::ButtonHAL> mButtonHal;
        std::unique_ptr<qifeng::LedHAL> mLedHal;
        std::unique_ptr<qifeng::RecordHAL> mRecordHal;
        std::unique_ptr<qifeng::ScreenHAL> mScreenHal;

        // 按键回调
        mutable std::shared_mutex mCallbackMutex;  // 读写锁
        ButtonCallback mButtonCallback;
        // LED回调
        LedEventCallback mLedCallback;

        // 初始化标志
        std::atomic<bool> mButtonInit {false};
        std::atomic<bool> mLedInit {false};
        std::atomic<bool> mRecordInit {false};
        std::atomic<bool> mDisplayInit {false};
        std::atomic<bool> mFingerprintInit {false};

        std::mutex mDisplayMutex;

        std::mutex mFormatMutex;
        AudioUtilsConfig mAudioFormat;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_HAL_HAL_BRIDGE_H
