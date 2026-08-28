/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * vad_worker.h - Silero VAD 语音活动检测 Worker，基于 Sophon TPU 推理
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_VAD_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_VAD_WORKER_H

#include <mutex>
#include <string>
#include <vector>

#include "common/config_manager.h"
#include "common/logger.h"
#include "worker.h"

namespace qifeng {

    // Silero VAD 语音活动检测 Worker
    // 继承 ModelWorker 基类，使用 Sophon TPU 进行 NPU 推理
    // 与 ModelsManager 的接口保持兼容：Inference / VadFeature / AsrFeature /
    // VpFeature
    class VADWorker : public ModelWorker {
    public:
        VADWorker(const std::string& modelPath, int tpuId = 0, IOMode ioMode = IOMode::SYSIO);
        ~VADWorker();

        // Silero VAD 推理：speechFeat 参数被忽略（Silero 直接从原始音频检测），仅使用
        // audioSample 返回 [[start_ms, end_ms], ...] 语音段列表
        std::vector<std::vector<int>> Inference(const FloatMatrix& speechFeat, const std::vector<float>& audioSample);

        // 兼容接口：输出三维分段（外层 batch=1），与 ModelsManager::VADProcess 配合
        std::vector<std::vector<std::vector<int>>> InferenceBModel(const FloatMatrix& speechFeat,
                                                                   const std::vector<float>& audioSample);

        // 特征提取方法（委托给 AudioFeature，供 ASR/SV 流程复用）
        FloatMatrix VadFeature(const std::vector<float>& audioSample);
        FloatMatrix AsrFeature(const std::vector<float>& audioSample);
        FloatMatrix VpFeature(const std::vector<float>& audioSample);

    private:
        // 重置 VAD 状态机
        void ResetStates();

        // 对单个音频块进行 NPU 推理并更新状态机
        void Predict(const std::vector<float>& dataChunk);

        // 初始化持久化 BmTensor（在构造函数中调用）
        bool InitPersistentTensors();

        // Silero VAD 常量参数
        static constexpr int kContextSamples = 64;        // 上下文窗口样本数
        static constexpr int kWindowSizeSamples = 512;    // 每帧样本数 (32ms @ 16kHz)
        static constexpr int kEffectiveWindowSize = 576;  // 有效窗口 = 512 + 64
        static constexpr int kSampleRate = 16000;         // 采样率 16kHz
        static constexpr int kStateSize = 2 * 1 * 128;    // 隐藏状态大小 (2x1x128)
        static constexpr float kBoundaryOffset = 0.15f;   // 边界区偏移：[threshold-0.15, threshold) 不做判断

        int mSrPerMs;  // 每毫秒采样数 = sample_rate / 1000

        // 上下文和状态缓冲区
        std::vector<float> mContext;  // 64 上下文样本
        std::vector<float> mState;    // 2x1x128 隐藏状态

        // VAD 参数（可配置）
        float mThreshold;                   // 语音概率阈值，默认 0.5
        int mMinSilenceSamples;             // 最小静音样本数（100ms → 1600 样本）
        int mMinSilenceSamplesAtMaxSpeech;  // 最大语音下的最小静音（98ms → 1568 样本）
        int mMinSpeechSamples;              // 最小语音样本数（250ms → 4000 样本）
        int64_t mMaxSpeechSamples;          // 最大语音样本数（默认无限制 = INT64_MAX）
        int mSpeechPadSamples;              // 语音起点回溯填充样本数（默认 30ms = 480 样本）
        int mSpeechPadEndSamples;           // 语音终点前向填充样本数（默认 0ms）

        // VAD 状态机变量
        bool mTriggered;                          // 是否正在检测语音段
        unsigned int mTempEnd;                    // 临时结束点
        unsigned int mCurrentSample;              // 当前处理到的样本位置
        int mPrevEnd;                             // 上一个结束点
        int mNextStart;                           // 下一个开始点
        std::vector<std::vector<int>> mSpeeches;  // [[start_sample, end_sample], ...]
        std::vector<int> mCurrentSpeech;          // 当前语音段 [start_sample, end_sample]

        // 预分配的推理缓冲区（避免每次 Predict 内堆分配）
        std::vector<float> mEffectiveData;  // 有效窗口数据 [kEffectiveWindowSize]

        // 持久化 BmTensor（跨 Predict 调用复用，避免每帧创建/销毁设备内存）
        // Silero bmodel 已将 sr 烘焙为常量，仅保留 input/state 两个输入
        std::unique_ptr<BmTensor> mInputTensor;   // input: [1, 576] float32
        std::unique_ptr<BmTensor> mStateTensor;   // state: [2, 1, 128] float32
        std::unique_ptr<BmTensor> mOutputTensor;  // output_Reshape: [1] float32
        std::unique_ptr<BmTensor> mStateNTensor;  // stateN_Concat: [2, 1, 128] float32
        bool mTensorsReady;                       // 张量是否已初始化

        std::mutex mInferenceMutex;  // 保护整个推理流程（状态机和所有成员变量）
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_VAD_WORKER_H
