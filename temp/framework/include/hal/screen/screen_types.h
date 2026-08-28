/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_SCREEN_TYPES_H
#define HAL_SCREEN_SCREEN_TYPES_H

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace qifeng {
    /**
     * @brief 显示屏页面
     */
    enum class DisplayPage : uint16_t { Idle = 0x0001, Meeting = 0x0002, Fingerprint = 0x0003, Upgrade = 0x0004 };

    /**
     * @brief 控件类型，区分空闲页与会议页的不同控件
     */
    enum class WidgetType : uint8_t {
        IdleAvailability,
        IdleSummary,
        IdleTask,
        IdleStatus,
        MeetingBasicInfo,
        MeetingInfo,
        MeetingEnergy,
        MeetingClock,
        DeviceInfo,
        Fingerprint,
        Upgrade,
    };

    struct IdleAvailabilityData {
        uint32_t availableTime = 0;  // 可用时长(h)
        uint16_t ratioLevel = 0;     // 可用时长饼图等级（1~12） # 例如：1 代表可用空间最大
        uint16_t ratioValue = 0;     // 可用时长百分比数值 # 例如：70
    };

    struct IdleSummaryData {
        uint32_t totalCount = 0;      // 总任务数
        uint32_t pendingCount = 0;    // 待办任务数
        uint32_t completedCount = 0;  // 已办任务数
        uint16_t ratioLevel = 0;  // 任务完成率饼图等级（1~22）# 例如：1 代表完成率最大 22 代表已生成和未生成都为0
    };

    struct IdleTaskData {
        uint16_t status = 0;  // 任务状态（1: 暂无待处理, 2: 会议待转写, 3: AI转写中, 4: 会议待总结, 5: AI总结中）
        std::string name {};             // 任务名称 # 例如："产品周会"
        std::string progressMinutes {};  // 任务已进行分钟数 # 例如："30"
        std::string totalMinutes {};     // 任务总分钟数 # 例如："60"
        std::string statusText {};       // 任务状态文本，只传 hh:mm 时间部分 # 例如："17:18"
        uint16_t ratioLevel = 0;  // 任务进度饼图等级（1~12）# 例如：1 代表进度最小，12 显示背景色
    };

    struct IdleStatusData {
        uint16_t status =
            1;  // 1: 待机状态(无图); 2: 开启录音失败提示(可用时长不足180分钟); 3: 开启录音失败（麦克风异常; 4: 会议暂停超时结束; 5: 会议超时自动结束）
    };

    struct MeetingClockData {
        uint16_t year = 0;
        uint16_t month = 0;
        uint16_t day = 0;
        uint16_t hour = 0;
        uint16_t minute = 0;
        uint16_t second = 0;
    };

    struct MeetingBasicData {
        std::string sponsor {};    // 会议发起人 # 例如："张三"
        std::string startTime {};  // 会议发起时间 # 例如："2026/01/01/00:00"
    };

    struct MeetingInfoData {
        uint16_t type = 0;  // 会议类型（1: 不展示图片, 2: 展示公共会议卡片, 3: 展示私密会议卡片 ）
        std::string name {};  // 会议名称 # 例如："产品周会"
        uint16_t operationTip = 0;  // 会议操作提示（1: 无提示窗, 2: 无权限结束会议, 3: 是否结束当前会议, 4: 操作频繁）
        uint16_t audioStatus = 0;       // 音频状态（1: 暂停, 2: 录制中, 3: 无音频输入）
        uint16_t ratioLevel = 0;        // 会议进度饼图等级（0~100）# 例如：1 代表进度最小
        MeetingClockData clockText {};  // 会议持续时长文本 # 例如："00:30:00"
        uint16_t countDownTip = 0;      // 会议倒计时卡片（1: 有倒计时卡片, 2: 无倒计时卡片）
        std::string countDown {};       // 会议倒计时提示内容 # 例如："30秒" 或者 "30分钟"
    };

    struct MeetingEnergyData {
        std::vector<uint16_t> energyValues {};  // 会议能量值（1~52，52为暂停）# 共50个数据点
    };

    struct DeviceInfoData {
        uint16_t wifiStatus = 0;   // 设备WIFI状态（1: 显示wifi, 2: 不显示wifi）
        std::string wifiSSID {};   // 设备WiFi SSID # 例如："ES-CA500-000001"
        std::string ipAddress {};  // 设备IP地址 # 例如："192.168.1.100"
    };

    struct FingerprintData {
        uint16_t ratioLevel = 0;  // 指纹录入进度（1~7）# 例如：1 代表录入进度为零
        std::string tip {};       // 指纹录入提示 # 例如："请抬起您的手指"
        // 协议文档"无图"状态统一值为1，正常情况默认无图
        uint16_t result = 1;  // 指纹录入结果（1: 正常情况(无图), 2: 成功, 3: 失败）
    };

    struct UpgradeData {
        uint16_t title = 0;    // 升级标题（1: 系统升级中, 2: 成功, 3: 失败）
        uint16_t types = 0;    // 升级类型（1: 系统升级中, 2: 成功, 3: 失败）
        uint16_t loading = 0;  // loading动画（0: 升级中, 1: 完成）
    };

    /**
     * @brief 控件数据变体，与 WidgetType 一一对应
     */
    using WidgetData = std::variant<IdleAvailabilityData, IdleSummaryData, IdleTaskData, IdleStatusData,
                                    MeetingBasicData, MeetingInfoData, MeetingEnergyData, MeetingClockData,
                                    DeviceInfoData, FingerprintData, UpgradeData>;
}  // namespace qifeng

#endif  // HAL_SCREEN_SCREEN_TYPES_H
