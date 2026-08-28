//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_DISPLAY_COLLECTOR_H
#define QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_DISPLAY_COLLECTOR_H

#include "internal/hal/hal_types.h"

namespace qifeng_ca {

    // 显示屏数据采集器: 从数据库/服务采集各页面所需的指标数据
    // 无状态, 线程安全
    class DisplayCollector {
    public:
        // 采集空闲页面指标(磁盘时长/纪要统计/任务状态)
        IdleMetrics CollectIdleMetrics();

        // 采集会议页面指标(会议名称/类型/进度/音频状态/弹窗)
        // popupTip: 当前弹窗tip(0=无弹窗), 由调用方从 PopupManager 获取
        MeetingMetrics CollectMeetingMetrics(uint16_t popupTip);

        // 采集设备信息(WiFi状态/SSID/IP)
        DeviceInfoMetrics CollectDeviceInfo();

    private:
        void CollectSummaryStats(IdleMetrics &metrics);
        void CollectTaskStatus(IdleMetrics &metrics);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_DISPLAY_COLLECTOR_H
