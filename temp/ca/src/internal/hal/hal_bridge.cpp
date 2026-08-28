//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <ctime>
#include <mutex>
#include <utility>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/utils/time.h"
#include "qifeng_framework/hal/button/button_hal.h"
#include "qifeng_framework/hal/record/record_hal.h"
#include "qifeng_framework/hal/screen/screen_hal.h"

#include "common/config/hal_config.h"
#include "internal/hal/fingerprint_bridge.h"
#include "internal/hal/hal_bridge.h"

namespace qifeng_ca {

    // 返回指定时间（time_t）所在本地时区的当天 00:00:00 对应的 UTC 时间戳
    // 若未传参，则默认使用当前系统时间
    static uint64_t StartOfDay(std::time_t t = std::time(nullptr)) {
        struct tm tmBuf {};
        // 转换为本地时区的 tm 结构（使用线程安全版本）
        if (localtime_r(&t, &tmBuf) == nullptr) {
            return 0;
        }
        // 保留日期，时分秒置零
        tmBuf.tm_hour = 0;
        tmBuf.tm_min = 0;
        tmBuf.tm_sec = 0;
        tmBuf.tm_isdst = -1;  // 让 mktime 自动判断夏令时
        // 将本地 tm 还原为 UTC 时间戳（mktime 自动处理时区与夏令时）
        return static_cast<uint64_t>(std::mktime(&tmBuf));
    }

    // ---- HalBridge实现 ----

    HalBridge &HalBridge::GetInstance() {
        static HalBridge Instance;
        return Instance;
    }

    bool HalBridge::InitAll() {
        bool ok = true;
        if (!InitDisplay()) {
            SLOG_ERROR << "HalBridge: display init failed";
            ok = false;
        }
        // 按键与指纹只要一个成功就行(按键与灯光绑定)
        if (HalConfig::GetInstance().IsSkipButton()) {
            SLOG_WARN << "HalBridge: skip button";
        } else {
            if (HalConfig::GetInstance().IsFingerprintBridge()) {
                if (!InitFingerprint()) {
                    SLOG_ERROR << "HalBridge: fingerprint init failed";
                    ok = false;
                }
            } else {
                if (!InitButton()) {
                    SLOG_ERROR << "HalBridge: button init failed";
                    ok = false;
                }
            }
        }
        if (!InitLed()) {
            SLOG_ERROR << "HalBridge: led init failed";
            // ok = false; // 集联灯光不影响整体初始化
        }

        if (!InitRecorder()) {
            SLOG_ERROR << "HalBridge: recorder init failed";
            ok = false;
        }
        // 默认值
        mAudioFormat.mSampleRate = HalConfig::GetInstance().GetFileRecordSampleRate();
        mAudioFormat.mChannels = HalConfig::GetInstance().GetFileRecordChannels();
        mAudioFormat.mBitDepth = HalConfig::GetInstance().GetFileRecordBitDepth();
        return ok;
    }

    void HalBridge::ReleaseAll() {
        FingerprintBridge::GetInstance().Release();
        if (mButtonInit.load(std::memory_order_acquire) && mButtonHal) {
            mButtonHal->Release();
            mButtonHal.reset();
            mButtonInit.store(false, std::memory_order_release);
        }
        if (mLedInit.load(std::memory_order_acquire) && mLedHal) {
            mLedHal->Release();
            mLedHal.reset();
            mLedInit.store(false, std::memory_order_release);
        }
        if (mRecordInit.load(std::memory_order_acquire) && mRecordHal) {
            mRecordHal->Release();
            mRecordHal.reset();
            mRecordInit.store(false, std::memory_order_release);
        }
        if (mDisplayInit.load(std::memory_order_acquire) && mScreenHal) {
            mScreenHal->Release();
            mScreenHal.reset();
            mDisplayInit.store(false, std::memory_order_release);
        }
    }

    bool HalBridge::InitFingerprint() {
        if (mFingerprintInit.load(std::memory_order_acquire)) {
            return true;
        }
        if (!FingerprintBridge::GetInstance().Init()) {
            return false;
        }
        mFingerprintInit.store(true, std::memory_order_release);
        return true;
    }

    // ---- 按键 ----

    bool HalBridge::InitButton() {
        if (mButtonInit.load(std::memory_order_acquire)) {
            return true;
        }

        auto &cfg = HalConfig::GetInstance();
        qifeng::ButtonConfig config;
        for (uint32_t i = 0; i <= cfg.GetButtonId(); ++i) {
            qifeng::ButtonItemConfig item;
            item.id = cfg.GetButtonId();
            item.type = qifeng::ButtonDeviceType::GPIO;
            item.gpio.chip = cfg.GetButtonGpioChip();
            item.gpio.line = cfg.GetButtonGpioLine();
            config.buttons.emplace_back(std::move(item));
        }

        mButtonHal = std::make_unique<qifeng::ButtonHAL>();
        if (!mButtonHal->Init(config)) {
            SLOG_ERROR << "HalBridge: ButtonHAL init failed";
            return false;
        }

        mButtonHal->SetButtonCallback(OnButtonEvent);
        mButtonInit.store(true, std::memory_order_release);
        SLOG_INFO << "HalBridge: button initialized";
        return true;
    }

    void HalBridge::SetButtonCallback(ButtonCallback callback) {
        std::unique_lock lock(mCallbackMutex);
        mButtonCallback = std::move(callback);
    }

    void HalBridge::OnButtonEvent(uint32_t buttonId, qifeng::ButtonEventType event) {
        auto &inst = GetInstance();
        ButtonCallback cb;
        {
            std::shared_lock lock(inst.mCallbackMutex);
            cb = inst.mButtonCallback;
        }
        if (!cb) {
            return;
        }

        ButtonEventData data;
        data.mButtonId = buttonId;
        data.mPressType =
            (event == qifeng::ButtonEventType::ShortPress) ? ButtonPressType::ShortPress : ButtonPressType::LongPress;
        cb(data);
    }

    // ---- LED ----

    bool HalBridge::InitLed() {
        if (mLedInit.load(std::memory_order_acquire)) {
            return true;
        }

        auto &cfg = HalConfig::GetInstance();
        qifeng::LedConfig config;
        for (uint32_t i = 0; i <= cfg.GetLedId(); ++i) {
            qifeng::LedItemConfig item;
            item.id = i;
            item.type = qifeng::LedDeviceType::CLI;
            item.gpio.red_gpio = cfg.GetLedRedGpio();
            item.gpio.green_gpio = cfg.GetLedGreenGpio();
            item.cli.tool_path = cfg.GetCliPath();
            item.cli.timeout_ms = cfg.GetCliTimeoutMs();
            config.leds.emplace_back(item);
        }

        mLedHal = std::make_unique<qifeng::LedHAL>();
        if (!mLedHal->Init(config)) {
            SLOG_ERROR << "HalBridge: LedHAL init failed";
            return false;
        }
        mLedHal->SetLedCallback(OnLedEvent);

        mLedInit.store(true, std::memory_order_release);
        SLOG_INFO << "HalBridge: led initialized";
        return true;
    }

    bool HalBridge::SetLedState(const LedState &state) {
        if (!mLedInit.load(std::memory_order_acquire)) {
            SLOG_WARN << "HalBridge: led not initialized";
            return false;
        }

        qifeng::LedMode mode = ConvertLedMode(state.mMode);
        if (!state.mOn) {
            mode = qifeng::LedMode::Off;
        }
        qifeng::LedColor color = ConvertLedColor(state.mColor);
        return mLedHal->SetColor(mode, color);
    }

    qifeng::LedColor HalBridge::ConvertLedColor(HalLedColor color) const {
        switch (color) {
            case HalLedColor::Red:
                return qifeng::LedColor::Red;
            case HalLedColor::Green:
                return qifeng::LedColor::Green;
            case HalLedColor::Blue:
                return qifeng::LedColor::Blue;
            case HalLedColor::Off:
            default:
                return qifeng::LedColor::Off;
        }
    }

    qifeng::LedMode HalBridge::ConvertLedMode(HalLedMode mode) const {
        switch (mode) {
            case HalLedMode::Solid:
            default:
                return qifeng::LedMode::On;
        }
    }

    void HalBridge::SetLedCallback(LedEventCallback callback) {
        mLedCallback = std::move(callback);
        // LedHAL 底层回调注册在 lifecycle 层完成
    }

    void HalBridge::OnLedEvent(uint32_t ledId, qifeng::LedEventType eventType) {
        auto &inst = GetInstance();
        auto cb = inst.mLedCallback;
        if (cb) {
            MicMuteEvent event = (eventType == qifeng::LedEventType::Mute) ? MicMuteEvent::Mute : MicMuteEvent::Unmute;
            cb(ledId, event);
        }
    }

    // ---- 录音 ----

    bool HalBridge::InitRecorder() {
        if (mRecordInit.load(std::memory_order_acquire)) {
            return true;
        }

        auto &cfg = HalConfig::GetInstance();
        qifeng::AudioConfig config;
        config.device.pcm_name = cfg.GetRecordPcmNames();
        // 黑名单: 屏蔽HDMI输入(hw:CARD=rockchiphdmiin)与板载ES8388(hw:CARD=rockchipes8388)等
        config.device.blacklist_pcm_names = cfg.GetRecordBlacklistPcmNames();
        config.format.sample_rate = cfg.GetRecordSampleRate();
        config.format.channels = cfg.GetRecordChannels();
        config.format.bit_depth = cfg.GetRecordBitDepth();
        config.behavior.latency_ms = cfg.GetRecordLatencyMs();
        config.behavior.ring_buffer_size = cfg.GetRecordRingBufferSize();

        mRecordHal = std::make_unique<qifeng::RecordHAL>();
        if (!mRecordHal->Init(config)) {
            SLOG_ERROR << "HalBridge: RecordHAL init failed";
            return false;
        }

        mRecordInit.store(true, std::memory_order_release);
        SLOG_INFO << "HalBridge: recorder initialized";
        return true;
    }

    bool HalBridge::StartRecording() {
        if (!mRecordInit.load(std::memory_order_acquire) || !mRecordHal) {
            SLOG_ERROR << "HalBridge: recorder not initialized";
            return false;
        }
        return mRecordHal->StartRecording();
    }

    bool HalBridge::StopRecording() {
        if (!mRecordInit.load(std::memory_order_acquire) || !mRecordHal) {
            SLOG_ERROR << "HalBridge: recorder not initialized";
            return false;
        }
        return mRecordHal->StopRecording();
    }

    bool HalBridge::IsRecordingUnavailable() const {
        if (!mRecordInit.load(std::memory_order_acquire) || !mRecordHal) {
            SLOG_ERROR << "HalBridge: recorder not initialized";
            return false;
        }
        return mRecordHal->GetState() == qifeng::AudioState::Unavailable;
    }

    bool HalBridge::IsRecordingUninitialized() const {
        if (!mRecordInit.load(std::memory_order_acquire) || !mRecordHal) {
            SLOG_ERROR << "HalBridge: recorder not initialized";
            return false;
        }
        return mRecordHal->GetState() == qifeng::AudioState::Uninitialized;
    }

    bool HalBridge::IsRecordingActive() const {
        if (!mRecordInit.load(std::memory_order_acquire) || !mRecordHal) {
            SLOG_ERROR << "HalBridge: recorder not initialized";
            return false;
        }
        return mRecordHal->GetState() == qifeng::AudioState::Recording;
    }

    bool HalBridge::IsRecordingIdle() const {
        if (!mRecordInit.load(std::memory_order_acquire) || !mRecordHal) {
            SLOG_ERROR << "HalBridge: recorder not initialized";
            return false;
        }
        return mRecordHal->GetState() == qifeng::AudioState::Idle;
    }

    bool HalBridge::HasMicrophone() const {
        if (!mRecordInit.load(std::memory_order_acquire) || !mRecordHal) {
            SLOG_WARN << "HalBridge: recorder not initialized";
            return false;
        }
        return mRecordHal->HasMicrophone();
    }

    size_t HalBridge::ReadAudio(std::vector<uint8_t> &outBuffer, size_t maxSize) {
        if (!mRecordInit.load(std::memory_order_acquire) || !mRecordHal) {
            SLOG_ERROR << "HalBridge: recorder not initialized";
            return 0;
        }
        return mRecordHal->Read(outBuffer, maxSize);
    }

    // ---- 录音采样率 ----

    AudioUtilsConfig HalBridge::GetAudioFormat() {
        auto format = mRecordHal->GetActualAudioFormat();
        if (format.sample_rate == 0 || format.channels == 0 || format.bit_depth == 0) {
            SLOG_ERROR << "HalBridge: recorder format not valid";
            std::lock_guard<std::mutex> lock(mFormatMutex);
            return mAudioFormat;
        }
        std::lock_guard<std::mutex> lock(mFormatMutex);
        mAudioFormat =
            AudioUtilsConfig {static_cast<uint32_t>(format.sample_rate), static_cast<uint16_t>(format.channels),
                              static_cast<uint16_t>(format.bit_depth)};
        SLOG_DEBUG << "HalBridge: GetAudioFormat, sampleRate=" << format.sample_rate << ", channels=" << format.channels
                   << ", bitDepth=" << format.bit_depth;
        return mAudioFormat;
    }

    // ---- 显示屏 ----

    bool HalBridge::InitDisplay() {
        if (!HalConfig::GetInstance().IsScreen()) {
            return true;
        }
        if (mDisplayInit.load(std::memory_order_acquire)) {
            return true;
        }

        auto &cfg = HalConfig::GetInstance();
        qifeng::DisplayConfig config;
        config.protocolType = qifeng::ScreenProtocolType::Diwen;
        config.transportType = qifeng::ScreenTransportType::Serial;
        config.transport.port = cfg.GetDisplaySerialPort();
        config.transport.baudRate = cfg.GetDisplayBaudRate();

        mScreenHal = std::make_unique<qifeng::ScreenHAL>();
        auto result = mScreenHal->Init(config);
        if (result != qifeng::ScreenResult::OK) {
            SLOG_ERROR << "HalBridge: ScreenHAL init failed, result=" << static_cast<int>(result);
            return false;
        }

        mDisplayInit.store(true, std::memory_order_release);
        SLOG_INFO << "HalBridge: display initialized";
        return true;
    }

    void HalBridge::DisplaySwitchIdle() {
        if (!HalConfig::GetInstance().IsScreen()) {
            return;
        }
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: Screen not initialized";
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);
        auto &cfg = HalConfig::GetInstance();
        SLOG_DEBUG << "HalBridge: DisplaySwitchIdle, timeoutMs=" << cfg.GetDisplayTimeoutMs();
        auto ret = mScreenHal->SwitchPage(qifeng::DisplayPage::Idle, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "DisplaySwitchIdle: ret=" << static_cast<int>(ret);
    }

    void HalBridge::DisplaySwitchMeeting() {
        if (!HalConfig::GetInstance().IsScreen()) {
            return;
        }
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: Screen not initialized";
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);
        auto &cfg = HalConfig::GetInstance();
        SLOG_DEBUG << "HalBridge: DisplaySwitchMeeting, timeoutMs=" << cfg.GetDisplayTimeoutMs();
        auto ret = mScreenHal->SwitchPage(qifeng::DisplayPage::Meeting, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "DisplaySwitchMeeting: ret=" << static_cast<int>(ret);
    }

    void HalBridge::DisplaySwitchFingerprint() {
        if (!HalConfig::GetInstance().IsScreen()) {
            return;
        }
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: Screen not initialized";
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);
        auto &cfg = HalConfig::GetInstance();
        SLOG_DEBUG << "HalBridge: DisplaySwitchFingerprint, timeoutMs=" << cfg.GetDisplayTimeoutMs();
        auto ret = mScreenHal->SwitchPage(qifeng::DisplayPage::Fingerprint, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "DisplaySwitchFingerprint: ret=" << static_cast<int>(ret);
    }

    void HalBridge::DisplayUpdateIdle(const IdleMetrics &metrics) {
        if (!HalConfig::GetInstance().IsScreen()) {
            return;
        }
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: Screen not initialized";
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);
        UpdateIdleTask(metrics);  // 先刷新task避免出现乱码
        UpdateIdleAvailability(metrics);
        UpdateIdleSummary(metrics);
        UpdateIdleStatus(metrics);
    }

    void HalBridge::DisplayAccessibleUpdateMeeting() {
        if (!HalConfig::GetInstance().IsScreen()) {
            return;
        }
        MeetingMetrics metrics;
        metrics.mOperationTip = 2;  // 2: 无权限结束会议
        SLOG_DEBUG << "HalBridge: DisplayAccessibleUpdateMeeting, operationTip=" << metrics.mOperationTip;
        // 仅刷新会议信息, 避免覆盖发起人/开始时间等基础信息
        HalBridge::GetInstance().DisplayUpdateMeetingInfoOnly(metrics);
    }

    void HalBridge::DisplayUpdateMeeting(const MeetingMetrics &metrics) {
        if (!HalConfig::GetInstance().IsScreen()) {
            return;
        }
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: Screen not initialized";
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);
        UpdateMeetingBasic(metrics);
        UpdateMeetingInfo(metrics);
    }

    void HalBridge::DisplayUpdateMeetingInfoOnly(const MeetingMetrics &metrics) {
        if (!HalConfig::GetInstance().IsScreen()) {
            return;
        }
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: Screen not initialized";
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);
        UpdateMeetingInfo(metrics);
    }

    void HalBridge::DisplayUpdateWaveform(const std::vector<int32_t> &waveform) {
        if (!HalConfig::GetInstance().IsScreen()) {
            return;
        }
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: Screen not initialized";
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);
        UpdateMeetingEnergy(waveform);
    }

    void HalBridge::DisplayUpdateDeviceInfo(const DeviceInfoMetrics &metrics) {
        if (!HalConfig::GetInstance().IsScreen()) {
            return;
        }
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: Screen not initialized";
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);
        UpdateDeviceInfo(metrics);
    }

    void HalBridge::DisplayUpdateFingerprint(const FingerprintDisplayData &data) {
        if (!HalConfig::GetInstance().IsScreen()) {
            return;
        }
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: Screen not initialized";
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);
        UpdateFingerprint(data);
    }

    void HalBridge::DisplaySetTime(const MeetingMetrics &metrics) {
        if (!HalConfig::GetInstance().IsScreen()) {
            return;
        }
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);

        qifeng::MeetingClockData clockData;
        time_t startTime = static_cast<time_t>(StartOfDay() + metrics.mTotalTime);
        struct tm tmBuf {};
        // 转换为本地时区的 tm 结构（使用线程安全版本）
        if (localtime_r(&startTime, &tmBuf) == nullptr) {
            return;
        }
        clockData.year = static_cast<uint16_t>(tmBuf.tm_year + 1900);
        clockData.month = static_cast<uint16_t>(tmBuf.tm_mon + 1);
        clockData.day = static_cast<uint16_t>(tmBuf.tm_mday);
        clockData.hour = static_cast<uint16_t>(tmBuf.tm_hour);
        clockData.minute = static_cast<uint16_t>(tmBuf.tm_min);
        clockData.second = static_cast<uint16_t>(tmBuf.tm_sec);

        SLOG_DEBUG << "HalBridge: DisplaySetTime, year=" << clockData.year << ", month=" << clockData.month
                   << ", day=" << clockData.day << ", hour=" << clockData.hour << ", minute=" << clockData.minute
                   << ", second=" << clockData.second;
        auto ret = mScreenHal->UpdateWidget(qifeng::WidgetType::MeetingClock, clockData,
                                            HalConfig::GetInstance().GetDisplayTimeoutMs());
        SLOG_DEBUG << "DisplaySetTime: ret=" << static_cast<int>(ret);
    }

    // ---- Display辅助: 转换数据类型 ----

    void HalBridge::UpdateIdleAvailability(const IdleMetrics &metrics) {
        auto &cfg = HalConfig::GetInstance();
        qifeng::IdleAvailabilityData data;
        data.availableTime = static_cast<uint32_t>(metrics.mAvailableTime);
        data.ratioLevel = static_cast<uint16_t>(metrics.mAvailableTimeProp);
        data.ratioValue = static_cast<uint16_t>(metrics.mAvailableTimeRatio);
        SLOG_DEBUG << "UpdateIdleAvailability: availableTime=" << data.availableTime
                   << ", ratioLevel=" << data.ratioLevel << ", ratioValue=" << data.ratioValue;
        auto ret = mScreenHal->UpdateWidget(qifeng::WidgetType::IdleAvailability, data, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "UpdateIdleAvailability: ret=" << static_cast<int>(ret);
    }

    void HalBridge::UpdateIdleSummary(const IdleMetrics &metrics) {
        auto &cfg = HalConfig::GetInstance();
        qifeng::IdleSummaryData data;
        data.completedCount = static_cast<uint32_t>(metrics.mSummaryDone);
        data.pendingCount = static_cast<uint32_t>(metrics.mSummaryWait);
        data.totalCount = static_cast<uint32_t>(metrics.mSummaryTotal);
        data.ratioLevel = static_cast<uint16_t>(metrics.mSummaryRatio);
        SLOG_DEBUG << "UpdateIdleSummary: completedCount=" << data.completedCount
                   << ", pendingCount=" << data.pendingCount << ", totalCount=" << data.totalCount
                   << ", ratioLevel=" << data.ratioLevel;
        auto ret = mScreenHal->UpdateWidget(qifeng::WidgetType::IdleSummary, data, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "UpdateIdleSummary: ret=" << static_cast<int>(ret);
    }

    void HalBridge::UpdateIdleTask(const IdleMetrics &metrics) {
        auto &cfg = HalConfig::GetInstance();
        qifeng::IdleTaskData data;
        data.status = static_cast<uint16_t>(metrics.mTaskStatus);
        data.name = metrics.mTaskMeetingName;
        data.progressMinutes = metrics.mTaskProgressText;
        data.totalMinutes = metrics.mTaskTotalTimeText;
        data.statusText = metrics.mTaskStatusText;
        data.ratioLevel = static_cast<uint16_t>(metrics.mTaskProportion);
        SLOG_DEBUG << "UpdateIdleTask: status=" << data.status << ", name=" << data.name
                   << ", progressMinutes=" << data.progressMinutes << ", totalMinutes=" << data.totalMinutes
                   << ", statusText=" << data.statusText << ", ratioLevel=" << data.ratioLevel;
        auto ret = mScreenHal->UpdateWidget(qifeng::WidgetType::IdleTask, data, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "UpdateIdleTask: ret=" << static_cast<int>(ret);
    }

    void HalBridge::UpdateIdleStatus(const IdleMetrics &metrics) {
        auto &cfg = HalConfig::GetInstance();
        qifeng::IdleStatusData data;
        data.status = static_cast<uint16_t>(metrics.mDeviceStatus);
        SLOG_DEBUG << "UpdateIdleStatus: status=" << data.status;
        auto ret = mScreenHal->UpdateWidget(qifeng::WidgetType::IdleStatus, data, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "UpdateIdleStatus: ret=" << static_cast<int>(ret);
    }

    void HalBridge::UpdateMeetingInfo(const MeetingMetrics &metrics) {
        auto &cfg = HalConfig::GetInstance();
        qifeng::MeetingInfoData data;
        data.type = static_cast<uint16_t>(metrics.mType);
        data.name = metrics.mMeetingName;
        data.operationTip = static_cast<uint16_t>(metrics.mOperationTip);
        data.audioStatus = static_cast<uint16_t>(metrics.mAudioStatus);
        data.ratioLevel = static_cast<uint16_t>(metrics.mProgress);

        time_t startTime = static_cast<time_t>(StartOfDay() + metrics.mTotalTime);
        struct tm tmBuf {};
        // 转换为本地时区的 tm 结构（使用线程安全版本）
        if (localtime_r(&startTime, &tmBuf) == nullptr) {
            return;
        }
        data.clockText.year = static_cast<uint16_t>(tmBuf.tm_year + 1900);
        data.clockText.month = static_cast<uint16_t>(tmBuf.tm_mon + 1);
        data.clockText.day = static_cast<uint16_t>(tmBuf.tm_mday);
        data.clockText.hour = static_cast<uint16_t>(tmBuf.tm_hour);
        data.clockText.minute = static_cast<uint16_t>(tmBuf.tm_min);
        data.clockText.second = static_cast<uint16_t>(tmBuf.tm_sec);

        data.countDownTip = metrics.mCountDownTip;
        data.countDown = metrics.mCountDown;
        SLOG_DEBUG << "UpdateMeetingInfo: type=" << data.type << ", name=" << data.name
                   << ", operationTip=" << data.operationTip << ", audioStatus=" << data.audioStatus
                   << ", ratioLevel=" << data.ratioLevel << ", clockText=" << data.clockText.year << "/"
                   << data.clockText.month << "/" << data.clockText.day << "/" << data.clockText.hour << ":"
                   << data.clockText.minute << ":" << data.clockText.second << ", startTime=" << startTime
                   << ", countDownTip=" << data.countDownTip << ", countDown=" << data.countDown;
        auto ret = mScreenHal->UpdateWidget(qifeng::WidgetType::MeetingInfo, data, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "UpdateMeetingInfo: ret=" << static_cast<int>(ret);
    }

    void HalBridge::UpdateMeetingBasic(const MeetingMetrics &metrics) {
        auto &cfg = HalConfig::GetInstance();
        qifeng::MeetingBasicData data;
        qifeng::MeetingClockData startTime;
        data.sponsor = metrics.mSponsor;
        // 将startTime(unix秒)转为MeetingClockData
        time_t localStartTime = static_cast<time_t>(metrics.mStartTime / 1000);
        struct tm tmBuf {};
        // 转换为本地时区的 tm 结构（使用线程安全版本）
        if (localtime_r(&localStartTime, &tmBuf) == nullptr) {
            return;
        }
        startTime.year = static_cast<uint16_t>(tmBuf.tm_year + 1900);
        startTime.month = static_cast<uint16_t>(tmBuf.tm_mon + 1);
        startTime.day = static_cast<uint16_t>(tmBuf.tm_mday);
        startTime.hour = static_cast<uint16_t>(tmBuf.tm_hour);
        startTime.minute = static_cast<uint16_t>(tmBuf.tm_min);
        startTime.second = static_cast<uint16_t>(tmBuf.tm_sec);

        data.startTime = fmt::format("{:04d}-{:02d}-{:02d}-{:02d}:{:02d}", startTime.year, startTime.month,
                                     startTime.day, startTime.hour, startTime.minute);
        SLOG_DEBUG << "UpdateMeetingBasic: sponsor=" << data.sponsor << ", startTime=" << data.startTime;
        auto ret = mScreenHal->UpdateWidget(qifeng::WidgetType::MeetingBasicInfo, data, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "UpdateMeetingBasic: ret=" << static_cast<int>(ret);
    }

    void HalBridge::UpdateMeetingEnergy(const std::vector<int32_t> &waveform) {
        auto &cfg = HalConfig::GetInstance();
        qifeng::MeetingEnergyData data;
        for (auto val : waveform) {
            data.energyValues.push_back(static_cast<uint16_t>(val));
        }
        auto ret = mScreenHal->UpdateWidget(qifeng::WidgetType::MeetingEnergy, data, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "UpdateMeetingEnergy: ret=" << static_cast<int>(ret);
    }

    void HalBridge::UpdateDeviceInfo(const DeviceInfoMetrics &metrics) {
        auto &cfg = HalConfig::GetInstance();
        qifeng::DeviceInfoData data;
        data.wifiStatus = metrics.mWifiConnected ? 1 : 2;  // 1: 显示wifi, 2: 不显示wifi
        data.wifiSSID = metrics.mWifiSSID;
        data.ipAddress = metrics.mIpAddress;
        SLOG_DEBUG << "UpdateDeviceInfo: wifiStatus=" << data.wifiStatus << ", wifiSSID=" << data.wifiSSID
                   << ", ipAddress=" << data.ipAddress;
        auto ret = mScreenHal->UpdateWidget(qifeng::WidgetType::DeviceInfo, data, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "UpdateDeviceInfo: ret=" << static_cast<int>(ret);
    }

    void HalBridge::UpdateFingerprint(const FingerprintDisplayData &data) {
        auto &cfg = HalConfig::GetInstance();
        qifeng::FingerprintData fpData;
        fpData.ratioLevel = data.mRatioLevel;
        fpData.tip = data.mTip;
        fpData.result = data.mResult;
        SLOG_DEBUG << "UpdateFingerprint: ratioLevel=" << fpData.ratioLevel << ", tip=" << fpData.tip
                   << ", result=" << fpData.result;
        auto ret = mScreenHal->UpdateWidget(qifeng::WidgetType::Fingerprint, fpData, cfg.GetDisplayTimeoutMs());
        SLOG_DEBUG << "UpdateFingerprint: ret=" << static_cast<int>(ret);
    }

    // ---- 升级页面 ----

    void HalBridge::DisplaySwitchUpgrade() {
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: Screen not initialized";
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);
        auto &cfg = HalConfig::GetInstance();
        SLOG_DEBUG << "HalBridge: DisplaySwitchUpgrade, timeoutMs=" << cfg.GetDisplayTimeoutMs();
        mScreenHal->SwitchPage(qifeng::DisplayPage::Upgrade, cfg.GetDisplayTimeoutMs());
    }

    void HalBridge::DisplayUpdateUpgrade(const UpgradeDisplayData &data) {
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: Screen not initialized";
            return;
        }
        std::lock_guard<std::mutex> lock(mDisplayMutex);
        UpdateUpgrade(data);
    }

    void HalBridge::UpdateUpgrade(const UpgradeDisplayData &data) {
        auto &cfg = HalConfig::GetInstance();
        qifeng::UpgradeData upData;
        upData.title = data.mTitle;
        upData.types = data.mTypes;
        upData.loading = data.mLoading;
        SLOG_DEBUG << "UpdateUpgrade: title=" << upData.title << ", types=" << upData.types
                   << ", loading=" << upData.loading;
        mScreenHal->UpdateWidget(qifeng::WidgetType::Upgrade, upData, cfg.GetDisplayTimeoutMs());
    }

    // ---- 屏幕固件升级 ----

    int HalBridge::UpgradeScreenFirmware(const std::vector<std::string> &filePaths) {
        if (!mDisplayInit.load(std::memory_order_acquire) || !mScreenHal) {
            SLOG_ERROR << "HalBridge: screen not initialized for firmware upgrade";
            return -1;
        }
        SLOG_INFO << "HalBridge: start screen firmware upgrade, fileCount=" << filePaths.size();
        auto result = mScreenHal->UpgradeFirmware(filePaths);
        if (result != qifeng::ScreenResult::OK) {
            SLOG_ERROR << "HalBridge: UpgradeFirmware failed, result=" << static_cast<int>(result);
            if (result == qifeng::ScreenResult::UpgradeInProgress) {
                return -2;
            }
            return -3;
        }
        SLOG_INFO << "HalBridge: screen firmware upgrade ok";
        return 0;
    }

}  // namespace qifeng_ca
