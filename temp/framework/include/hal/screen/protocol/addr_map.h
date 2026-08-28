/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_SCREEN_PROTOCOL_ADDR_MAP_H
#define HAL_SCREEN_PROTOCOL_ADDR_MAP_H

#include <cstdint>

namespace qifeng {
    namespace Addr {
        // 页面切换
        inline constexpr uint16_t PageSwitch = 0x0084;  // 页面切换

        // 可用时长
        inline constexpr uint16_t AvailableTime = 0x3000;        // 可用时长
        inline constexpr uint16_t AvailableRatioLevel = 0x3001;  // 可用时长等级
        inline constexpr uint16_t AvailableRatioText = 0x3050;   // 可用时长等级文本

        // 纪要
        inline constexpr uint16_t SummaryTotalCount = 0x3006;      // 纪要总数
        inline constexpr uint16_t SummaryPendingCount = 0x3004;    // 纪要待完成
        inline constexpr uint16_t SummaryCompletedCount = 0x3002;  // 纪要已完成
        inline constexpr uint16_t SummaryRatioLevel = 0x3008;      // 纪要等级

        // 任务
        inline constexpr uint16_t TaskStatus = 0x3009;        // 任务状态
        inline constexpr uint16_t TaskName = 0x3383;          // 任务名称(滚动文本，内容从VP+3开始)
        inline constexpr uint16_t TaskProgressText = 0x301A;  // 任务进度文本
        inline constexpr uint16_t TaskTotalTime = 0x301B;     // 任务总时间文本
        inline constexpr uint16_t TaskStatusText = 0x301C;    // 任务状态文本
        inline constexpr uint16_t TaskRatioLevel = 0x3022;    // 任务进度等级

        // 设备状态
        inline constexpr uint16_t DeviceStatus = 0x3023;  // 设备状态

        // 会议中信息
        inline constexpr uint16_t MeetingType = 0x3361;          // 会议类型
        inline constexpr uint16_t MeetingName = 0x302F;          // 会议名称
        inline constexpr uint16_t MeetingAudioStatus = 0x3044;   // 音频状态
        inline constexpr uint16_t MeetingClock = 0x3189;         // 会议中持续时长
        inline constexpr uint16_t MeetingRatioLevel = 0x3045;    // 会议进度等级
        inline constexpr uint16_t MeetingOperationTip = 0x3155;  // 会议操作提示
        inline constexpr uint16_t MeetingCountDownTip = 0x3458;  // 会议倒计时卡片
        inline constexpr uint16_t MeetingCountDown = 0x345A;     // 会议倒计时提示内容

        // 会议中基本信息
        inline constexpr uint16_t MeetingSponsor = 0x3400;    // 会议发起人
        inline constexpr uint16_t MeetingStartTime = 0x315C;  // 会议发起时间

        // 会议中能量数据
        inline constexpr uint16_t MeetingEnergy = 0x3100;  // 会议能量数据

        // 固件升级地址
        inline constexpr uint16_t FlashCacheBase = 0x8000;    // 升级数据缓存起始地址（每包 +0x78）
        inline constexpr uint16_t FlashTriggerAddr = 0x00AA;  // Flash 写入触发与完成状态查询地址
        inline constexpr uint16_t RebootAddr = 0x0004;        // 重启命令写入地址
        inline constexpr uint16_t DgusStopEnable = 0x00FC;    // 停止 DGUS 刷新/OS 核

        // 设备信息
        inline constexpr uint16_t DeviceWifiStatus = 0x3217;  // 设备WIFI状态
        inline constexpr uint16_t DeviceWifiSSID = 0x3223;    // 设备WIFI SSID
        inline constexpr uint16_t DeviceIPAddress = 0x3144;   // 设备IP地址

        // 指纹录入信息
        inline constexpr uint16_t FingerprintRatioLevel = 0x316E;  // 指纹录入进度
        inline constexpr uint16_t FingerprintTip = 0x3170;         // 指纹录入提示
        inline constexpr uint16_t FingerprintResult = 0x3215;      // 指纹录入结果

        // 显示屏升级页面
        inline constexpr uint16_t UpdateSystemTip = 0x320C;    // 显示屏升级标题
        inline constexpr uint16_t UpdateSystemTypes = 0x3216;  // 显示屏升级类型
        inline constexpr uint16_t UpgradeLoading = 0x3185;     // 升级loading动画（0: 升级中, 1: 完成）
    }  // namespace Addr
}  // namespace qifeng

#endif  // HAL_SCREEN_PROTOCOL_ADDR_MAP_H
