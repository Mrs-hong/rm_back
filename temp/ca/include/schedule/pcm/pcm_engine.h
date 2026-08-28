//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_SCHEDULE_PCM_PCM_ENGINE_H
#define QIFENG_CA_INCLUDE_SCHEDULE_PCM_PCM_ENGINE_H

#include <string>

#include "schedule/pcm/task_scheduler.h"

namespace qifeng_ca {

    // PCM引擎: 调度子系统的外观入口
    class PcmEngine {
    public:
        static PcmEngine &GetInstance();

        ~PcmEngine();

        PcmEngine(const PcmEngine &) = delete;
        PcmEngine &operator=(const PcmEngine &) = delete;
        PcmEngine(PcmEngine &&) = delete;
        PcmEngine &operator=(PcmEngine &&) = delete;

        // 初始化: 从DB恢复未完成任务并提交到TaskScheduler
        // 必须在Run之前调用
        bool Init();

        // 启动调度线程(委托TaskScheduler::Run)
        bool Run();

        // 停止调度线程(委托TaskScheduler::Stop)
        void Stop();

        // ---- 提交接口(委托TaskScheduler) ----

        void Submit(TaskPtr task);

        // ---- 查询接口(委托TaskScheduler) ----

        bool IsRunning(const std::string &audioId) const;
        bool IsInOfflineTrans(const std::string &audioId) const;
        bool IsInSummary(const std::string &audioId) const;

        // 获取队列中最高优先级任务(用于display)
        TaskPtr TopTask() const;

        // 获取当前正在运行的第一个非临时并发任务(用于display)
        TaskPtr CurrentRunningTask() const;

        // ---- 控制接口(委托TaskScheduler) ----

        int StopSummaryTask(const std::string &audioId);
        void CancelTaskByAudioId(const std::string &audioId);

    protected:
        PcmEngine() = default;

    private:
        std::shared_ptr<TaskScheduler> mTaskScheduler;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_SCHEDULE_PCM_PCM_ENGINE_H
