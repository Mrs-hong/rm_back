//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//
#include <algorithm>

#include "qifeng_framework/common/logger.h"

#include "common/common.h"
#include "dao_managers/meeting_dao_manager.h"
#include "schedule/pcm/task_scheduler.h"

namespace qifeng_ca {

    // 查询音频录音时间并缓存到任务上(仅在未缓存时查询DB), 用于同优先级队列按创建时间排序
    static void EnsureAudioCreateTime(const TaskPtr &task) {
        if (task->GetAudioCreateTime() != 0) {
            return;
        }
        auto &audioDao = MeetingDaoManager::GetInstance();
        auto audio = audioDao.GetByAudioIdGlobal(task->GetAudioId());
        if (audio.mTimestamp > 0) {
            task->SetAudioCreateTime(audio.mTimestamp);
        } else {
            // 查询失败时用当前时间兜底, 避免相同时间戳导致顺序不确定
            task->SetAudioCreateTime(static_cast<int64_t>(GetTimeMs()));
        }
    }

    static bool IsSameAudio(const TaskPtr &task, const std::string &audioId) {
        return task->GetAudioId() == audioId;
    }

    static bool IsSummaryTask(const TaskPtr &task) {
        return task->GetTaskType() == "Summary";
    }

    static bool IsOfflineTransTask(const TaskPtr &task) {
        return task->GetTaskType() == "OfflineTrans";
    }

    // 高优先级任务: 优先级数值<=Voiceprint(即Recording/Voiceprint, 实时任务)
    static bool IsHighPriorityTask(const TaskPtr &task) {
        return task->Priority() <= static_cast<int>(BaseTask::Priority::Voiceprint);
    }

    bool TaskScheduler::Run() {
        bool expected = false;
        if (!mIsRunning.compare_exchange_strong(expected, true)) {
            SLOG_WARN << "TaskScheduler: already running";
            return false;
        }
        mThread = std::make_unique<std::thread>(&TaskScheduler::Worker, this);
        SLOG_INFO << "TaskScheduler: started";
        return true;
    }

    void TaskScheduler::Stop() {
        bool expected = true;
        if (!mIsRunning.compare_exchange_strong(expected, false)) {
            return;
        }
        mCv.notify_all();
        if (mThread && mThread->joinable()) {
            mThread->join();
        }
        SLOG_INFO << "TaskScheduler: stopped";
    }

    void TaskScheduler::Submit(TaskPtr task) {
        if (!task) {
            return;
        }
        // 在加锁前查询音频创建时间, 避免持锁期间访问DB
        EnsureAudioCreateTime(task);
        {
            std::lock_guard<std::mutex> lock(mMutex);
            EnqueueLocked(task);
        }
        mCv.notify_one();
    }

    // 去重检查: 同一audioId且同类型已在队列中时不重复入队(临时并发任务除外)
    bool TaskScheduler::IsDuplicateInQueue(const TaskPtr &task) const {
        if (task->IsTemporaryConcurrent()) {
            return false;
        }
        const std::string &audioId = task->GetAudioId();
        const std::string_view taskType = task->GetTaskType();
        for (const auto &kv : mPriorityQueue) {
            for (const auto &queued : kv.second) {
                if (queued->GetAudioId() == audioId && queued->GetTaskType() == taskType) {
                    SLOG_WARN << "TaskScheduler: duplicate task already in queue, skip enqueue, audioId=" << audioId
                              << " taskType=" << task->GetTaskType();
                    return true;
                }
            }
        }
        return false;
    }

    void TaskScheduler::EnqueueLocked(const TaskPtr &task) {
        if (IsDuplicateInQueue(task)) {
            return;
        }
        SLOG_INFO << "TaskScheduler: enqueue, audioId=" << task->GetAudioId() << " taskType=" << task->GetTaskType()
                  << " priority=" << task->Priority();
        int priority = task->Priority();
        auto &list = mPriorityQueue[priority];
        // 按音频创建时间(录音时间)插入, 保证同一优先级队列内先创建的音频先执行
        auto it = std::upper_bound(list.begin(), list.end(), task, [](const TaskPtr &a, const TaskPtr &b) {
            return a->GetAudioCreateTime() < b->GetAudioCreateTime();
        });
        list.insert(it, task);
    }

    bool TaskScheduler::EraseFromQueueLocked(const TaskPtr &task) {
        int priority = task->Priority();
        auto it = mPriorityQueue.find(priority);
        if (it == mPriorityQueue.end()) {
            return false;
        }
        auto &list = it->second;
        auto before = list.size();
        list.erase(std::remove(list.begin(), list.end(), task), list.end());
        bool erased = list.size() < before;
        if (list.empty()) {
            mPriorityQueue.erase(it);
        }
        return erased;
    }

    TaskPtr TaskScheduler::PopTopTask() {
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto it = mPriorityQueue.begin(); it != mPriorityQueue.end(); ++it) {
            auto &list = it->second;
            if (list.empty()) {
                continue;
            }
            TaskPtr task = list.front();
            list.erase(list.begin());
            if (list.empty()) {
                mPriorityQueue.erase(it);
            }
            return task;
        }
        return nullptr;
    }

    std::vector<TaskPtr> TaskScheduler::CollectAndRemoveByAudioIdLocked(const std::string &audioId) {
        std::vector<TaskPtr> toCancel;
        // 收集运行中的任务(不从mRunningTasks移除, 由SweepCompleted在IsComplete后清理)
        for (auto &task : mRunningTasks) {
            if (IsSameAudio(task, audioId)) {
                toCancel.push_back(task);
            }
        }
        // 从优先级队列中移除并收集(队列中的任务也需Cancel标记complete, 防止后续被调度)
        for (auto it = mPriorityQueue.begin(); it != mPriorityQueue.end();) {
            auto &list = it->second;
            for (auto &t : list) {
                if (IsSameAudio(t, audioId)) {
                    toCancel.push_back(t);
                }
            }
            list.erase(
                std::remove_if(list.begin(), list.end(), [&](const TaskPtr &t) { return IsSameAudio(t, audioId); }),
                list.end());
            if (list.empty()) {
                it = mPriorityQueue.erase(it);
            } else {
                ++it;
            }
        }
        return toCancel;
    }

    void TaskScheduler::CancelByAudioId(const std::string &audioId) {
        SLOG_INFO << "TaskScheduler: cancel by audioId=" << audioId;
        // 在锁外调用Cancel, 避免Cancel内部逻辑(如NotifyWaiter获取mWaitMutex)与mMutex形成锁顺序风险
        std::vector<TaskPtr> toCancel;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            toCancel = CollectAndRemoveByAudioIdLocked(audioId);
        }
        for (auto &task : toCancel) {
            task->Cancel();
        }
    }

    bool TaskScheduler::TryStartTask(const TaskPtr &task) {
        bool revived = false;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            // 防竞态: peek(释放锁)到此处(重新加锁)之间, 任务可能已被CancelByAudioId从队列移除.
            if (EraseFromQueueLocked(task)) {
                mRunningTasks.push_back(task);
                revived = true;
            }
        }
        if (revived) {
            SLOG_INFO << "TaskScheduler: starting task, audioId=" << task->GetAudioId()
                      << " type=" << task->GetTaskType() << " size=" << QueueSize();
            task->Start();
        } else {
            SLOG_INFO << "TaskScheduler: task cancelled before start, skip, audioId=" << task->GetAudioId();
        }
        return revived;
    }

    int TaskScheduler::StopSummaryTask(const std::string &audioId) {
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto &task : mRunningTasks) {
            if (IsSameAudio(task, audioId) && IsSummaryTask(task)) {
                task->Stop();
                return 0;
            }
        }
        return -1;
    }

    bool TaskScheduler::IsRunning(const std::string &audioId) const {
        std::lock_guard<std::mutex> lock(mMutex);
        return HasAudioInRunning(audioId);
    }

    bool TaskScheduler::IsInOfflineTrans(const std::string &audioId) const {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto &task : mRunningTasks) {
            if (IsSameAudio(task, audioId) && IsOfflineTransTask(task)) {
                return true;
            }
        }
        return false;
    }

    bool TaskScheduler::IsInSummary(const std::string &audioId) const {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto &task : mRunningTasks) {
            if (IsSameAudio(task, audioId) && IsSummaryTask(task)) {
                return true;
            }
        }
        return false;
    }

    size_t TaskScheduler::QueueSize() const {
        std::lock_guard<std::mutex> lock(mMutex);
        size_t total = 0;
        for (const auto &kv : mPriorityQueue) {
            total += kv.second.size();
        }
        return total;
    }

    TaskPtr TaskScheduler::TopTask() const {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto &kv : mPriorityQueue) {
            if (!kv.second.empty()) {
                return kv.second.front();
            }
        }
        return nullptr;
    }

    TaskPtr TaskScheduler::CurrentRunningTask() const {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto &task : mRunningTasks) {
            if (!task->IsTemporaryConcurrent()) {
                return task;
            }
        }
        return nullptr;
    }

    void TaskScheduler::Worker() {
        SLOG_INFO << "TaskScheduler: worker started";
        while (mIsRunning.load(std::memory_order_acquire)) {
            {
                std::unique_lock<std::mutex> lock(mMutex);
                // 默认sleep 100ms后再处理队列(节流, 避免高频唤醒产生大量日志);
                // 仅在收到停止信号时提前唤醒, 不在任务提交时立即唤醒
                mCv.wait_for(lock, std::chrono::milliseconds(100), [this]() { return !mIsRunning.load(); });
            }
            // 先清理已完成任务(触发After链式调用, 可能产生新任务)
            SweepCompleted();
            // 再尝试启动下一个任务
            ProcessNext();
        }
        SLOG_INFO << "TaskScheduler: worker exited";
    }

    TaskPtr TaskScheduler::PeekNextTask(TaskPtr &concurrentFallback) {
        TaskPtr task;
        std::lock_guard<std::mutex> lock(mMutex);
        // 从优先级队列中取出最高优先级任务(不删除, 仅查看)
        // 同时收集更低优先级队列中的临时并发任务作为备选,
        // 以便在主候选槽位不足时仍能启动可并行的临时并发任务(如分段声纹),
        // 避免低优先级临时并发任务被持续饥饿
        for (auto it = mPriorityQueue.begin(); it != mPriorityQueue.end(); ++it) {
            // 跳过在排队期间被取消/完成的任务, 直接从队列移除
            while (!it->second.empty() && it->second.front()->IsComplete()) {
                SLOG_INFO << "TaskScheduler: skipping completed task in queue, audioId="
                          << it->second.front()->GetAudioId();
                it->second.erase(it->second.begin());
            }
            if (it->second.empty()) {
                continue;
            }
            if (!task) {
                task = it->second.front();
                // 临时并发任务自身不受槽位限制, 无需备选
                if (task->IsTemporaryConcurrent()) {
                    break;
                }
            } else if (!concurrentFallback && it->second.front()->IsTemporaryConcurrent()) {
                concurrentFallback = it->second.front();
            }
        }
        return task;
    }

    void TaskScheduler::ProcessNext() {
        TaskPtr concurrentFallback;  // 主候选不可运行时的备选临时并发任务
        TaskPtr task = PeekNextTask(concurrentFallback);
        if (!task) {
            return;
        }

        // 优先级抢占: 只要存在比当前任务优先级更低的运行中任务, 抢占最低优先级的那个
        //    (同级别不抢占). 抢占不依赖槽位状态, 仅比较优先级.
        //    抢占后需等待被抢占任务退出运行态并由SweepCompleted清理, 下一轮再启动当前任务.
        if (TryPreemptFor(task)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return;
        }

        // 无需抢占, 检查是否有空闲槽位
        if (HasSlotFor(task)) {
            TryStartTask(task);
            return;
        }

        // 主候选槽位不足, 尝试启动备选临时并发任务(不受槽位限制, 可与其他任务并行)
        if (concurrentFallback) {
            TryStartTask(concurrentFallback);
        }
    }

    bool TaskScheduler::HasSlotFor(const TaskPtr &task) const {
        // 临时并发任务可突破mMaxPriority限制(如分段声纹可与其他任务并行)
        if (task->IsTemporaryConcurrent()) {
            return true;
        }
        if (IsHighPriorityTask(task)) {
            // 高优先级: 同类型互斥(同时只允许一个录音/声纹运行)
            for (const auto &running : mRunningTasks) {
                if (IsHighPriorityTask(running)) {
                    return false;
                }
            }
            return true;
        }
        // 保证优先级槽位不被临时并发任务占满
        size_t lowCount = 0;
        for (const auto &running : mRunningTasks) {
            if (running->IsTemporaryConcurrent()) {
                continue;
            }
            ++lowCount;
        }
        return lowCount < mMaxPriority;
    }

    bool TaskScheduler::TryPreemptFor(const TaskPtr &incoming) {
        // 临时并发任务可与其他任务并行
        if (incoming->IsTemporaryConcurrent()) {
            return false;
        }
        int incomingPriority = incoming->Priority();
        TaskPtr victim;
        int victimPriority = incomingPriority;  // 仅抢占严格更低优先级(数值更大)的任务

        {
            std::lock_guard<std::mutex> lock(mMutex);
            for (const auto &running : mRunningTasks) {
                // 跳过不可抢占、已完成或已停止(被抢占/主动停止但尚未sweep)的任务
                if (!running->IsPreemptable() || running->IsComplete() || !running->IsRunning()) {
                    continue;
                }
                int runningPriority = running->Priority();
                // 同级别不抢占, 仅抢占严格更低优先级(数值更大)的任务;
                // 多个候选时选优先级最低(数值最大)的那个
                if (runningPriority > victimPriority) {
                    victimPriority = runningPriority;
                    victim = running;
                }
            }
        }

        if (!victim) {
            return false;
        }

        SLOG_INFO << "TaskScheduler: preempting task, audioId=" << victim->GetAudioId()
                  << " victimPriority=" << victimPriority << " incomingPriority=" << incomingPriority;
        victim->Preempt();

        // 被抢占任务从运行列表移除并重新加入队列等待下次调度
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mRunningTasks.erase(std::remove(mRunningTasks.begin(), mRunningTasks.end(), victim), mRunningTasks.end());
            EnqueueLocked(victim);
        }
        return true;
    }

    bool TaskScheduler::HasAudioInRunning(const std::string &audioId) const {
        for (const auto &task : mRunningTasks) {
            if (IsSameAudio(task, audioId)) {
                return true;
            }
        }
        return false;
    }

    void TaskScheduler::SweepCompleted() {
        std::vector<TaskPtr> completed;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            auto it = mRunningTasks.begin();
            while (it != mRunningTasks.end()) {
                if ((*it)->IsComplete() || (*it)->IsCanceled()) {
                    completed.push_back(*it);
                    it = mRunningTasks.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // 在锁外触发After(), 避免死锁(After可能Submit新任务)
        for (const auto &task : completed) {
            OnTaskComplete(task);
        }
    }

    void TaskScheduler::OnTaskComplete(const TaskPtr &task) {
        SLOG_INFO << "TaskScheduler: task complete, audioId=" << task->GetAudioId() << " type=" << task->GetTaskType();
        // 已取消的任务不执行后续链式调用(如After提交纪要), 避免为已取消的音频产生新任务
        if (task->IsCanceled()) {
            SLOG_INFO << "TaskScheduler: task was canceled, skip After, audioId=" << task->GetAudioId()
                      << " type=" << task->GetTaskType();
            return;
        }
        // 触发链式调用: 如录音完成->提交纪要, 离线转写完成->提交纪要
        task->After();
    }

}  // namespace qifeng_ca
