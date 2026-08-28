//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_HAL_HAL_TYPES_H
#define QIFENG_CA_INCLUDE_INTERNAL_HAL_HAL_TYPES_H

#include <cstdint>
#include <string>

namespace qifeng_ca {

    // ---- HAL相关数据类型定义(BMS-HAL抽象层) -----

    // LED闪烁模式
    enum class HalLedMode : uint8_t {
        Solid = 0,  // 常亮
        // BlinkFast,  // 快闪(磁盘严重告警)
        // BlinkSlow   // 慢闪(磁盘软限制告警)
    };

    // LED颜色枚举(与qifeng::LedColor对齐, 增加可扩展性)
    enum class HalLedColor : uint8_t { Off = 0, Red, Green, Blue };

    // LED状态配置
    struct LedState {
        bool mOn {false};
        HalLedColor mColor {HalLedColor::Red};
        HalLedMode mMode {HalLedMode::Solid};
    };

    // 待机页面指标数据
    struct IdleMetrics {
        int32_t mDeviceStatus {0};
        int32_t mAvailableTime {0};
        int32_t mAvailableTimeProp {0};
        uint16_t mAvailableTimeRatio {0};
        int32_t mSummaryDone {0};
        int32_t mSummaryWait {0};
        int32_t mSummaryTotal {0};
        int32_t mSummaryRatio {0};
        int32_t mTaskStatus {1};
        std::string mTaskMeetingName;
        std::string mTaskStatusText;
        std::string mTaskProgressText;
        std::string mTaskTotalTimeText;
        int32_t mTaskProportion {0};
    };

    // 会议页面指标数据
    struct MeetingMetrics {
        int64_t mStartTime {0};
        std::string mSponsor;  // 会议发起人
        std::string mMeetingName;
        uint32_t mType {1};          // 会议类型(1: 不展示卡片，2: 公共/访客, 3: 私密/非访客)
        uint32_t mOperationTip {1};  // 操作提示(1: 无提示, 2: 无权限结束, 3: 是否结束, 4: 操作频繁)
        int32_t mAudioStatus {2};    // 音频状态(1: 暂停, 2: 录制中, 3: 无音频输入)
        int32_t mProgress {0};       // 会议进度(0~100, 基于会议时长/3小时上限计算)
        int32_t mTotalTime {0};      // 会议总时长(ms)
        uint16_t mCountDownTip {0};  // 会议倒计时卡片(1: 有倒计时卡片, 2: 无倒计时卡片)
        std::string mCountDown;      // 会议倒计时提示内容, 如"30秒" "30分钟" "2小时" "1天"
    };

    // 设备信息(显示屏WiFi/IP展示)
    struct DeviceInfoMetrics {
        bool mWifiConnected {false};
        std::string mWifiSSID;
        std::string mIpAddress;
    };

    // 指纹录入显示屏数据
    struct FingerprintDisplayData {
        uint16_t mRatioLevel {1};  // 录入进度(1~7, 1=零进度)
        std::string mTip;          // 提示文本
        uint16_t mResult {1};      // 结果(1: 不展示, 2: 成功, 3: 失败)
    };

    // 升级页面显示屏数据
    struct UpgradeDisplayData {
        uint16_t mTitle {0};    // 升级标题（1: 系统升级中, 2: 成功, 3: 失败）
        uint16_t mTypes {0};    // 升级类型（1: 系统升级中, 2: 成功, 3: 失败）
        uint16_t mLoading {0};  // loading动画（0: 升级中, 1: 完成）
    };

    // 按键事件类型
    enum class ButtonPressType : uint8_t { ShortPress = 0, LongPress };

    // 按键事件数据
    struct ButtonEventData {
        uint32_t mButtonId {0};
        ButtonPressType mPressType {ButtonPressType::ShortPress};
    };

    // 指纹事件类型
    enum class FingerprintEventType : uint8_t {
        FingerDown = 0,  // 手指按下
        FingerUp,        // 手指抬起
        Record,          // 识别记录(匹配结果)
        Recognizing,     // 识别中
        RecognizingEnd,  // 识别结束
    };

    // 指纹事件数据
    struct FingerprintEventData {
        FingerprintEventType mType {FingerprintEventType::FingerDown};
        bool mMatched {false};
        uint16_t mFingerId {0xFFFF};
        uint16_t mScore {0};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_HAL_HAL_TYPES_H
