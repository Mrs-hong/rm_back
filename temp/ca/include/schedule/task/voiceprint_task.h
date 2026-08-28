//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_SCHEDULE_TASK_VOICEPRINT_TASK_H
#define QIFENG_CA_INCLUDE_SCHEDULE_TASK_VOICEPRINT_TASK_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "aas/aas_callback.h"
#include "common/status.h"
#include "schedule/task/base_task.h"

namespace qifeng_ca {

    // 声纹录制结果
    struct VoiceprintResult {
        Status mStatus;
        std::vector<std::vector<float>> mSvEmbedding;
        std::string mSvEmbeddingMd5;
        std::string mFilePath;  // 录制音频文件路径
    };

    // 声纹录制完成回调
    using VoiceprintCallback = std::function<void(const VoiceprintResult &)>;

    // 声纹录制任务(高优先级, 不可抢占): HAL录音 -> 内存缓存 -> 超时/手动停止/取消 -> AAS声纹提取 -> 异步回调
    class VoiceprintTask : public BaseTask {
    public:
        VoiceprintTask(const std::string &audioId, uint64_t accountId);

        ~VoiceprintTask() override;

        VoiceprintTask(const VoiceprintTask &) = delete;
        VoiceprintTask(VoiceprintTask &&) = delete;
        VoiceprintTask &operator=(const VoiceprintTask &) = delete;
        VoiceprintTask &operator=(VoiceprintTask &&) = delete;

        // BaseTask接口
        void Start() override;
        void Stop() override;
        void Cancel() override;
        bool IsPreemptable() const override { return false; }
        bool IsTemporaryConcurrent() const override { return true; }
        int Priority() const override { return static_cast<int>(Priority::Voiceprint); }
        std::string_view GetTaskType() const override { return "Voiceprint"; }

        // 设置回调(在Start前调用)
        void SetCallback(const VoiceprintCallback &cb) { mCallback = cb; }

        // 主动停止并同步等待AAS结果, 超时返回错误
        VoiceprintResult StopAndWait(int waitTimeoutMs = 10000);

        bool IsWaitingForAas() const { return mIsTimeout.load(std::memory_order_acquire); }

    private:
        // 获取自身shared_ptr(用于Workflow定时器回调)
        std::shared_ptr<VoiceprintTask> Self() { return std::static_pointer_cast<VoiceprintTask>(shared_from_this()); }

        // 定时读取循环: 读HAL -> 重采样 -> 缓存到内存
        void DoReadCycle();

        // 调度下一次读取(Workflow timer)
        void ScheduleNext();

        // 录制结束: 将缓存的PCM数据提交到AAS声纹提取
        void OnRecordingFinished();

        // 将PCM数据写入WAV文件
        void WriteWavFile();

        // 构建ResultInfo并提交到AAS StartSVJob(异步, 结果通过SendCallBack回调)
        void SubmitToAas();

        // AAS StartSVJob的SendCallBack回调: 接收声纹提取结果
        void OnAasResult(qifeng::aas::AasResult aasResult);

        // 通知StopAndWait的等待线程
        void NotifyWaiter(const VoiceprintResult &result);

        // 通知结果(通过VoiceprintCallback回调)
        void NotifyResult(const VoiceprintResult &result);

        // 超时定时器回调(默认60s后自动停止)
        void OnTimeout();

        // 启动超时定时器
        void StartTimeoutTimer();

        // 停止读取循环(不触发AAS)
        void StopReadLoop();

        // 取消本任务关联的所有定时器
        void StopTimers();

    private:
        // HAL录音格式
        int mRecordSampleRate {0};
        int mRecordChannel {0};
        int mRecordBitDepth {0};
        // HAL格式下1秒的字节数
        size_t mHalOneSecBytes {0};

        // AAS目标格式(重采样目标)
        qifeng::aas::FormatConfig mAasFormatConfig;

        // 内存缓存: 重采样后的PCM数据
        std::vector<uint8_t> mPcmBuffer;
        size_t mPcmDataSize {0};

        std::mutex mReadMutex;

        // 回调
        VoiceprintCallback mCallback;

        // StopAndWait同步等待
        std::mutex mWaitMutex;
        std::condition_variable mWaitCv;
        VoiceprintResult mWaitResult;
        bool mWaitNotified {false};

        // 任务已超时, 自动提交AAS
        std::atomic<bool> mIsTimeout {false};

        static constexpr int ReadIntervalMs = 100;
        // 默认录制时长(s)
        static constexpr int DefaultRecordDurationSec = 60;

        // 本任务的读取循环定时器名称(唯一,支持cancel_by_name)
        std::string mReadTimerName;
        // 本任务的超时时定时器名称(唯一,支持cancel_by_name)
        std::string mTimeoutTimerName;
        // WAV文件路径
        std::string mFilePath;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_SCHEDULE_TASK_VOICEPRINT_TASK_H
