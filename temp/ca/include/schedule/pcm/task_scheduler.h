//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_SCHEDULE_PCM_TASK_SCHEDULER_H
#define QIFENG_CA_INCLUDE_SCHEDULE_PCM_TASK_SCHEDULER_H

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "schedule/task/base_task.h"

namespace qifeng_ca {

    // 任务调度器: 系统唯一的任务调度核心
    // 槽位策略:
    //   - 高优先级任务(Recording/Voiceprint): 同类型互斥, 同时只允许一个运行
    //   - 低优先级任务(OfflineTrans/Summary): 受mMaxPriority限制并发数
    //   - 临时并发任务(IsTemporaryConcurrent): 可突破低优先级上限(如分段声纹)
    //
    // 注: 该调度器是全局单例, 被PcmEngine包装对外暴露生命周期入口
    class TaskScheduler {
    public:
        TaskScheduler() = default;

        // 启动调度线程
        bool Run();

        // 停止调度线程
        void Stop();

        // 提交任务到队列
        void Submit(TaskPtr task);

        // 取消指定音频的所有任务(运行中+队列中)
        void CancelByAudioId(const std::string &audioId);

        // 停止指定音频的纪要任务
        int StopSummaryTask(const std::string &audioId);

        // 查询
        bool IsRunning(const std::string &audioId) const;
        bool IsInOfflineTrans(const std::string &audioId) const;
        bool IsInSummary(const std::string &audioId) const;

        // 队列大小
        size_t QueueSize() const;

        // 获取队列中最高优先级任务(用于display)
        TaskPtr TopTask() const;

        // 获取当前正在运行的第一个非临时并发任务(用于display)
        TaskPtr CurrentRunningTask() const;

    private:
        // 工作线程: 等待任务 -> 清理已完成 -> 尝试启动下一个
        void Worker();

        // 尝试启动队列中的下一个任务
        void ProcessNext();

        // 取出最高优先级任务(从mPriorityQueue中)
        TaskPtr PopTopTask();

        // 尝试为待启动任务抢占: 若存在比待启动任务优先级更低的运行中任务,
        // 抢占其中优先级最低(且可抢占)的一个. 同级别不抢占.
        bool TryPreemptFor(const TaskPtr &incoming);

        // 检查是否有空闲槽位启动任务
        bool HasSlotFor(const TaskPtr &task) const;

        // 检查是否有指定音频的任务在运行
        bool HasAudioInRunning(const std::string &audioId) const;

        // 从运行列表中移除已完成任务
        void SweepCompleted();

        // 任务完成回调: 触发After()链式调用(如录音完成->提交转写/纪要任务)
        void OnTaskComplete(const TaskPtr &task);

        // 将任务加入优先级队列
        void EnqueueLocked(const TaskPtr &task);

        // 去重检查: 同一audioId且同类型已在队列中时不重复入队(临时并发任务除外)
        bool IsDuplicateInQueue(const TaskPtr &task) const;

        // 从优先级队列中移除任务, 返回是否实际移除(用于ProcessNext防竞态: 任务可能在peek后被CancelByAudioId移除)
        bool EraseFromQueueLocked(const TaskPtr &task);

        // 收集指定audioId的任务(运行中+队列中)并从队列移除, 返回待Cancel的任务列表
        // 注: 仅收集与移除, 不调用Cancel(由调用方在锁外调用, 避免锁顺序风险)
        std::vector<TaskPtr> CollectAndRemoveByAudioIdLocked(const std::string &audioId);

        // 尝试启动任务: 持锁校验任务仍在队列后移除并加入mRunningTasks, 锁外调用Start()
        bool TryStartTask(const TaskPtr &task);

        // 查看队列最高优先级任务(不删除), 同时收集一个备选临时并发任务
        // 跳过排队期间被取消/完成的任务. 返回主候选(可能为空).
        TaskPtr PeekNextTask(TaskPtr &concurrentFallback);

    private:
        // 优先级队列: key=优先级数值(越小越高), value=该优先级下的任务列表(按音频创建时间排序, 先创建先执行)
        std::map<int, std::vector<TaskPtr>> mPriorityQueue;
        std::vector<TaskPtr> mRunningTasks;
        size_t mMaxPriority {1};  // 优先级并发上限

        mutable std::mutex mMutex;
        std::condition_variable mCv;
        std::atomic<bool> mIsRunning {false};
        std::unique_ptr<std::thread> mThread;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_SCHEDULE_PCM_TASK_SCHEDULER_H
