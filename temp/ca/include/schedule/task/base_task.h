//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_SCHEDULE_TASK_BASE_TASK_H
#define QIFENG_CA_INCLUDE_SCHEDULE_TASK_BASE_TASK_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace qifeng_ca {

    // 任务基类: 所有PCM任务(录音、离线转写、纪要、声纹)的抽象基类
    // 生命周期: Submit -> Start -> [Preempt -> re-queue] -> Complete -> After -> Destroy
    // 派生类通过GetTaskType()返回自身的任务类型标识(字符串), 由调度器/外部判断
    class BaseTask : public std::enable_shared_from_this<BaseTask> {
    public:
        // 任务优先级(数值越小越高)
        // Recording/Voiceprint为最高优先级(实时任务, 可抢占低优先级任务)
        enum class Priority : int {
            Recording = 0,          // 实时录音(最高优先级)
            Voiceprint = 0,         // 实时声纹录音(最高优先级, 与录音同级)
            FileRealtimeTrans = 1,  // 文件模拟实时转写(测试用)
            Summary = 2,            // 纪要总结
            OfflineTrans = 3,       // 离线转写
            VoiceprintSegment = 4,  // 分段声纹(可临时并发)
        };

        virtual ~BaseTask();

        BaseTask(const BaseTask &) = delete;
        BaseTask(BaseTask &&) = delete;
        BaseTask &operator=(const BaseTask &) = delete;
        BaseTask &operator=(BaseTask &&) = delete;

        // 启动任务(异步, 结果通过内部回调通知)
        virtual void Start() = 0;

        // 停止任务(可恢复, 会被重新加入队列)
        virtual void Stop() {}

        // 取消任务(不可恢复, 不会被重新加入队列)
        virtual void Cancel() {
            mIsCanceled.store(true, std::memory_order_release);
            Stop();
        }

        // 是否可被高优先级任务抢占
        virtual bool IsPreemptable() const { return true; }

        // 抢占: 停止当前任务并返回是否需要重新入队
        virtual void Preempt() { Stop(); }

        // 优先级(数值越小越高), 由派生类按自身语义实现
        virtual int Priority() const = 0;

        // 是否可临时并发: 此类任务可暂时突破mMaxLowPriority限制(如分段声纹可与其他任务并行)
        virtual bool IsTemporaryConcurrent() const { return false; }

        // 任务完成后执行(如: 创建下一个任务)
        virtual void After() {}

        // 任务类型标识(用于调度器/日志/类型判断)
        virtual std::string_view GetTaskType() const = 0;

        // 状态查询
        bool IsComplete() const { return mComplete.load(std::memory_order_acquire); }
        bool IsRunning() const { return mRunning.load(std::memory_order_acquire); }
        bool IsCanceled() const { return mIsCanceled.load(std::memory_order_acquire); }

        // Getters
        const std::string &GetAudioId() const { return mAudioId; }
        const std::string &GetTaskId() const { return mTaskId; }
        uint64_t GetAccountId() const { return mAccountId; }

        // 音频创建时间(录音时间, 毫秒时间戳), 由调度器在入队时从DB查询并缓存,
        // 用于同优先级队列内按音频创建时间排序(先创建的先执行)
        int64_t GetAudioCreateTime() const { return mAudioCreateTime; }
        void SetAudioCreateTime(int64_t t) { mAudioCreateTime = t; }

    protected:
        BaseTask(std::string audioId, uint64_t accountId);

        void SetComplete();
        void SetRunning(bool running);
        void SetTaskId(const std::string &taskId) { mTaskId = taskId; }

    protected:
        std::string mAudioId;
        uint64_t mAccountId {0};
        std::string mTaskId;
        int64_t mAudioCreateTime {0};
        std::atomic<bool> mRunning {false};
        std::atomic<bool> mComplete {false};
        std::atomic<bool> mIsCanceled {false};
    };

    using TaskPtr = std::shared_ptr<BaseTask>;

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_SCHEDULE_TASK_BASE_TASK_H
