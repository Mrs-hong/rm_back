//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_SCHEDULE_TASK_AUDIO_READ_TASK_H
#define QIFENG_CA_INCLUDE_SCHEDULE_TASK_AUDIO_READ_TASK_H

#include <cstdint>
#include <memory>
#include <model/evaluator.h>
#include <mutex>
#include <string>
#include <vector>

#include "aas/aas_callback.h"
#include "common/utils/dual_wav_writer.h"
#include "common/utils/voice_energy.h"
#include "internal/aas/bms_audio_provider.h"
#include "internal/qwen_trans_merger.h"
#include "schedule/task/base_task.h"

namespace qifeng_ca {

    class TranscribeHandler;

    // 录音任务(高优先级, 不可抢占): 启动录音 -> 启动AAS实时转写 -> 读HAL音频 -> 写WAV -> 推AAS -> 更新波形
    class AudioReadTask : public BaseTask {
    public:
        AudioReadTask(const std::string &audioId, uint64_t accountId);

        ~AudioReadTask() override;

        AudioReadTask(const AudioReadTask &) = delete;
        AudioReadTask(AudioReadTask &&) = delete;
        AudioReadTask &operator=(const AudioReadTask &) = delete;
        AudioReadTask &operator=(AudioReadTask &&) = delete;

        // BaseTask接口
        void Start() override;
        void Stop() override;
        bool IsPreemptable() const override { return false; }
        void After() override;
        int Priority() const override { return static_cast<int>(Priority::Recording); }
        std::string_view GetTaskType() const override { return "Recording"; }

        // 设置系统消息通知器启用（访客时需要）
        void EnableSystemMessageNotifier() { mSystemMessageNotifierAll = true; }

        // 修复未正常关闭的WAV文件(如：断电重启后调用)
        static bool RepairWavFile(const std::string &filePath);

    private:
        // 单次读取循环: 读HAL -> 写WAV -> 推AAS -> 更新波形
        void DoReadCycle();

        // 调度下一次读取(Workflow timer -> go_task)
        void ScheduleNext();

        // 录音结束: 回填WAV头 + 通知AAS
        bool OnRecordingStart();
        void OnRecordingStopped();
        void OnRecordingPuased();

        // WAV文件操作
        void OpenWavFile();
        void WriteWavData(const uint8_t* data, size_t len);
        void FinalizeWavFile();

        // 录音结束后迁移到软连接目录: 更新DB file_name + 删除data下文件
        void MigrateToSymlinkAfterRecording();

        // 推送音频帧到AAS
        void ForwardToAas(const uint8_t* data, size_t len);

        // 更新波形到显示屏
        void UpdateWaveform(const uint8_t* data, size_t len);

        // 无音频输入检测: 按新增能量帧更新连续无声计数与RecordingManager状态
        void UpdateSilentDetection(const std::vector<int32_t> &newFrames);

        // 推送能量数据到WebSocket客户端
        void PushEnergyData(const int32_t volumeNum);

        // 将能量数组按1s均匀分发到多个定时任务
        void ScheduleEnergyDispatch(const std::vector<int32_t> &energyArr);

        // 将新增能量帧按100ms间隔逐点推入显示屏缓冲区并刷新
        void ScheduleDisplayRefresh(const std::vector<int32_t> &newFrames);

        // 定时器回调: 向波形缓冲区追加单帧并刷新显示屏
        void PushDisplayFrame(int32_t energy);

        // 构建显示屏波形数据
        std::vector<int32_t> BuildDisplayBufferLocked();

        // 尝试截断缓存中对齐1秒整数倍的音频数据并处理
        void TryProcessAlignedAudio();

        // 处理音频数据(重采样判断 + 写WAV + 推AAS + 更新波形)
        void DispatchAudioData(const uint8_t* data, size_t len);

        // 录音结束时刷新缓存中不足1秒的残余数据
        void FlushRemainingAudio();

        // 检查实时转写数据丢失
        bool CheckTransDataLoss();

        // 提交离线转写任务(数据丢失时)
        void SubmitOfflineTransTask();

        // 提交总结任务(无数据丢失时)
        void SubmitSummaryTaskInternal();

        // 根据mDataSize计算录音时长(毫秒)
        int32_t CalculateRecordingTime() const;

        // 更新数据库中的录音总时长(最多1秒一次)
        void UpdateTotalTimeToDb();

        // 录音达到上限: 提示显示屏+通知前端+主动RecordStop
        void HandleRecordLimitReached();

        // 暂停/恢复AAS provider(由DoReadCycle根据RecordingManager暂停状态驱动)
        void PauseProvider();
        void ResumeProvider();

        // 首次进入暂停: 暂停provider, 发送静音波形, 通知前端
        void HandleFirstPause();

        // 暂停超时: 停止会议并通知
        void HandlePauseTimeout(int64_t maxPauseMs, int64_t elapsedMs);

        // 限速刷新暂停倒计时显示屏
        void UpdatePauseCountdown(int64_t maxPauseMs, int64_t elapsedMs);

    private:
        // 获取自身shared_ptr(用于Workflow定时器回调)
        std::shared_ptr<AudioReadTask> Self() { return std::static_pointer_cast<AudioReadTask>(shared_from_this()); }

        bool CheckRecording();

        // 启动AAS实时转写
        bool StartAasTrans();

        // 停止AAS转写(信号end+等待drain+通知handler+停止AAS job)
        // 用于录音自然结束和主动停止两种场景
        void StopAasTrans();

        // Paraformer结果回调: 转发现有机制推送前端/落库(不做匹配缓存)
        void OnParaformerResult(const std::shared_ptr<TranscribeHandler> &handler,
                                const qifeng::aas::AasResult &result);

        // QWen结果回调: 转发给QwenTransMerger(匹配/替换/落库/推送全部由合并器处理)
        void OnQwenResult(const qifeng::aas::AasResult &result);

        // 启动读取循环
        void StartReadCycle();

    private:
        // 重采样配置
        int mRecordSampleRate;
        int mRecordChannel;
        int mRecordBitDepth;

        // AAS目标格式(重采样目标)
        qifeng::aas::FormatConfig mAasFormatConfig;

        // WAV文件
        std::string mWavFilePath;
        DualWavWriter mDualWriter;
        uint64_t mDataSize {0};
        bool mHeaderWritten {false};

        // HAL读取统计
        uint64_t mTotalHalBytes {0};

        // 已处理音频字节数(HAL格式, 1秒整数倍, 即WAV内容时长, 用于精确时长上限控制)
        uint64_t mProcessedBytes {0};
        // 是否已达到配置时长上限(达到后丢弃多余音频并触发停止, 防止重复触发)
        bool mLimitReached {false};

        // HAL原始数据缓存(按1秒对齐截断后处理)
        std::vector<uint8_t> mAudioBuffer;

        // HAL格式下1秒的字节数
        size_t mOneSecBytes {0};

        // 时长上限，保证截断阈值与total_time回填值的一致性
        uint64_t mLimitBytes {0};  // 上限对应的精确字节数(1秒整数倍, 字节数即精确音频时长)
        int32_t mLimitTimeMs {0};  // 上限对应的精确时长(毫秒), 用于total_time回填

        // 波形计算
        VoiceEnergyCalculator mEnergyCalc;
        std::vector<int32_t> mWaveformBuffer;
        // 显示屏波形缓冲区互斥锁(定时器回调与读循环可能并发访问)
        std::mutex mWaveformMutex;

        // 连续零能量帧计数(每帧100ms, 满100帧=10s判定为无音频输入)
        int mSilentFrameCount {0};

        // 能量分发定时器名
        std::string mEnergyDispatchTimerName;

        // 是否需要提交总结任务(录音时长过短时为false)
        bool mNeedSummary {true};

        // AAS provider与handler
        std::shared_ptr<BmsAudioProvider> mProvider;
        std::shared_ptr<TranscribeHandler> mHandler;
        // QWen模型provider(累计30s数据调用一次)
        std::shared_ptr<BmsAudioProvider> mQwenProvider;
        // QWen结果与DB记录整合合并器(匹配/替换/缓存/落库/推送, 独立模块解耦)
        // shared_ptr: QWen回调lambda持有merger副本, stop后最后一段QWen结果晚于reset返回时
        // 仍可安全处理(merger内部mStopped兜底落库), 避免静默丢弃
        std::shared_ptr<QwenTransMerger> mQwenMerger;
        bool mProviderPaused {false};
        int64_t mPauseStartTimeMs {0};
        int64_t mLastCountdownUpdateMs {0};
        std::string mLastCountdownText;  // 暂时倒计时限速

        // 系统消息通知器
        bool mSystemMessageNotifierAll {false};

        // 读取间隔(ms)
        static constexpr int ReadIntervalMs = 100;

        // 录音时长更新节流
        int64_t mLastTotalTimeUpdate {0};
        static constexpr int64_t TotalTimeUpdateIntervalMs = 1000;

        // 显示屏能量值展示数据点数量
        static constexpr size_t WaveformDisplayCount = 50;
        // 显示屏逐点刷新间隔(ms), 模拟前端100ms一点的滚动效果
        static constexpr int64_t DisplayRefreshIntervalMs = 100;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_SCHEDULE_TASK_AUDIO_READ_TASK_H
