//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <WFTask.h>
#include <cstdint>
#include <exception>
#include <string>

#include "qifeng_framework/common/logger.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"

#include "common/common.h"
#include "common/config/device_config.h"
#include "common/proto_utils.h"
#include "common/timer_manager.h"
#include "core/device/device_collect_task.h"
#include "core/device/device_collector.h"
#include "dao/device_dao.h"
#include "dao/models/bms_device.h"

namespace qifeng_ca {

    DeviceCollectTask &DeviceCollectTask::GetInstance() {
        static DeviceCollectTask Instance;
        return Instance;
    }

    void DeviceCollectTask::DoCollect() {
        try {
            SystemInfoItem item;
            if (!mCollector.CollectSystemInfo(item)) {
                SLOG_ERROR << "DeviceCollectTask: collect device info failed";
                return;
            }

            models::DeviceRecord record;
            record.mDeviceId = item.device_id();
            record.mTimestamp = static_cast<int64_t>(item.timestamp()) * 1000;

            SerializeSystemInfoToRecord(item, record);

            DeviceDao dao;
            if (!dao.InsertRecord(record)) {
                SLOG_ERROR << "DeviceCollectTask: insert device record failed";
                return;
            }

            SLOG_DEBUG << "DeviceCollectTask: device info collected, deviceId=" << record.mDeviceId;
        } catch (const std::exception &e) {
            SLOG_ERROR << "DeviceCollectTask::DoCollect exception: " << e.what();
        } catch (...) {
            SLOG_ERROR << "DeviceCollectTask::DoCollect unknown exception";
        }
    }

    void DeviceCollectTask::SerializeSystemInfoToRecord(const SystemInfoItem &item, models::DeviceRecord &record) {
        if (item.has_cpu()) {
            record.mCpu = proto_utils::MessageToJson(item.cpu());
        }
        if (item.has_memory()) {
            record.mMemory = proto_utils::MessageToJson(item.memory());
        }
        if (item.has_disk()) {
            record.mDisk = proto_utils::MessageToJson(item.disk());
        }
        if (item.has_accelerator()) {
            record.mAccelerator = proto_utils::MessageToJson(item.accelerator());
        }
        if (item.has_battery_info()) {
            record.mBattery = proto_utils::MessageToJson(item.battery_info());
        }
        if (item.has_audio()) {
            record.mAudio = proto_utils::MessageToJson(item.audio());
        }
        if (item.has_connect()) {
            record.mConnect = proto_utils::MessageToJson(item.connect());
        }

        VersionInfo versionInfo;
        for (int i = 0; i < item.versions_size(); ++i) {
            auto* version = versionInfo.add_versions();
            *version = item.versions(i);
        }
        record.mVersion += proto_utils::MessageToJson(versionInfo);
    }

    void DeviceCollectTask::ScheduleNext(int intervalSec) {
        if (!mIsRunning) {
            return;
        }

        // 使用命名定时器(支持优雅退出时cancel_by_name)
        auto* timerTask = WFTaskFactory::create_timer_task(
            std::string(TimerName::DeviceCollectTimer), static_cast<time_t>(intervalSec), 0,
            [this, intervalSec](WFTimerTask* task) {
                // 检查timer状态, 防止取消后异常回调
                if (task->get_state()) {
                    SLOG_ERROR << "DeviceCollectTimer: timer callback, state: " << task->get_state()
                               << ", error: " << task->get_error();
                    return;
                }
                if (!mIsRunning) {
                    return;
                }
                auto* collectTask = WFTaskFactory::create_go_task(WorkflowTakeName::DeviceCollectTask.data(),
                                                                  [this]() { this->DoCollect(); });

                auto* series = Workflow::create_series_work(
                    collectTask, [this, intervalSec](const SeriesWork*) { ScheduleNext(intervalSec); });
                series->start();
            });

        timerTask->start();
    }

    void DeviceCollectTask::Start(int intervalSec) {
        if (mIsRunning) {
            SLOG_WARN << "DeviceCollectTask already running";
            return;
        }

        mIsRunning = true;
        if (intervalSec <= 0) {
            intervalSec = DeviceConfig::GetInstance().GetCollectIntervalSec();
        }
        mIntervalSec = intervalSec;
        SLOG_INFO << "DeviceCollectTask started, interval=" << intervalSec
                  << "s, arch=" << DeviceConfig::GetInstance().GetArchName();

        auto* firstCollect =
            WFTaskFactory::create_go_task(WorkflowTakeName::DeviceTask.data(), [this]() { this->DoCollect(); });

        auto* series = Workflow::create_series_work(
            firstCollect, [this, intervalSec](const SeriesWork*) { ScheduleNext(intervalSec); });
        series->start();
    }

    void DeviceCollectTask::Stop() {
        mIsRunning = false;
        SLOG_INFO << "DeviceCollectTask stopped";
    }

}  // namespace qifeng_ca
