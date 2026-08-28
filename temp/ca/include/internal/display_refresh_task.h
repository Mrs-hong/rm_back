//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_REFRESH_TASK_H
#define QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_REFRESH_TASK_H

#include <atomic>

#include "internal/hal/display/display_page.h"

namespace qifeng_ca {

    // 显示屏定时刷新任务: 固定1s周期按当前页面分流刷新
    // Idle: 刷新空闲页+设备信息; Meeting(暂停/异常): 刷新会议页+时间; 录制中: 跳过(由AudioReadTask驱动)
    class DisplayRefreshTask {
    public:
        static DisplayRefreshTask &GetInstance();

        // 启动定时刷新任务(intervalSec固定为1s)
        void Start();

        // 停止任务
        void Stop();

        bool IsRunning() const;

    private:
        DisplayRefreshTask() = default;
        ~DisplayRefreshTask() = default;
        DisplayRefreshTask(const DisplayRefreshTask &) = delete;
        DisplayRefreshTask &operator=(const DisplayRefreshTask &) = delete;
        DisplayRefreshTask(DisplayRefreshTask &&) = delete;
        DisplayRefreshTask &operator=(DisplayRefreshTask &&) = delete;

        // 调度下一次定时刷新
        void ScheduleNext();

        // 执行一次刷新: 录音中跳过, 否则刷新空闲页面
        void DoRefresh();

        std::atomic<bool> mIsRunning {false};
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_INTERNAL_DISPLAY_REFRESH_TASK_H
