//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_DEVICE_DEVICE_COLLECT_TASK_H
#define QIFENG_CA_INCLUDE_CORE_DEVICE_DEVICE_COLLECT_TASK_H

#include "core/device/device_collector.h"
#include "dao/models/bms_device.h"
#include "qifeng_ca/device.pb.h"

namespace qifeng_ca {

    // TODO(yf): 后续可统一注册到定时监听管理中
    class DeviceCollectTask {
    public:
        static DeviceCollectTask &GetInstance();

        void Start(int intervalSec = 0);

        void Stop();

        bool IsRunning() const;

    private:
        DeviceCollectTask() = default;
        ~DeviceCollectTask() = default;
        DeviceCollectTask(const DeviceCollectTask &) = delete;
        DeviceCollectTask &operator=(const DeviceCollectTask &) = delete;
        DeviceCollectTask(DeviceCollectTask &&) = delete;
        DeviceCollectTask &operator=(DeviceCollectTask &&) = delete;

        void ScheduleNext(int intervalSec);

        // void ScheduleNext(WFTimerTask* timer) {}
        void DoCollect();

        static void SerializeSystemInfoToRecord(const SystemInfoItem &item, models::DeviceRecord &record);

        bool mIsRunning = false;
        int mIntervalSec = 5;
        DeviceCollector mCollector;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_DEVICE_DEVICE_COLLECT_TASK_H
