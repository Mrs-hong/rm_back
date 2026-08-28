/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * vad_worker.h - Silero VAD 语音活动检测 Worker，基于 Houmo TCIM 推理
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_VAD_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_VAD_WORKER_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/config_manager.h"
#include "common/logger.h"
#include "models_hm/worker/worker.h"

// onnxruntime 前置声明（仅用于 CPU 后端，成员以 unique_ptr 形式持有）
namespace Ort {
    class Env;
    class Session;
    class SessionOptions;
    class MemoryInfo;
}  // namespace Ort

namespace qifeng {

    // Silero VAD 语音活动检测 Worker
    // 继承 ModelWorker 基类；根据模型文件后缀自动选择后端：
    //   - .hmm  -> TCIM (Houmo M50 TPU)
    //   - .onnx -> onnxruntime (CPU)
    // 与 ModelsManager 的接口保持兼容：Inference / VadFeature / AsrFeature / VpFeature
    class VADWorker : public ModelWorker {
    public:
        // 推理后端类型
        enum class Backend {
            TCIM,      // TPU 推理
            ONNX_CPU,  // CPU 推理 (onnxruntime)
        };

        VADWorker(const std::string& modelPath, int tpuId = 0, IOMode ioMode = IOMode::SYSIO);
        ~VADWorker();

        // Silero VAD 推理：speechFeat 参数被忽略（Silero 直接从原始音频检测），仅使用 audioSample
        // 返回 [[start_ms, end_ms], ...] 语音段列表
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

        // 对单个音频块进行推理并更新状态机（按 mBackend 分发）
        void Predict(const std::vector<float>& dataChunk);

        // TCIM (TPU) 单帧推理
        int PredictTCIM();

        // onnxruntime (CPU) 单帧推理
        int PredictOnnx();

        // 初始化 onnxruntime CPU 会话（仅当模型为 .onnx 时调用）
        bool InitOnnxSession(const std::string& modelPath);

        // 清理 onnxruntime 资源
        void CleanupOnnx();

        // 初始化持久化 TCIM Tensor（在构造函数中调用）
        // TCIM Module 内部托管输入输出张量内存，这里仅需保留 host 缓冲区
        bool InitPersistentTensors();

        // 当前使用的推理后端
        Backend mBackend = Backend::TCIM;
        bool mBackendReady = false;  // 后端是否就绪（TCIM 用基类 IsReady，onnx 用此标志）

        // Silero VAD 常量参数
        static constexpr int kContextSamples = 64;        // 上下文窗口样本数
        static constexpr int kWindowSizeSamples = 512;    // 每帧样本数 (32ms @ 16kHz)
        static constexpr int kEffectiveWindowSize = 576;  // 有效窗口 = 512 + 64
        static constexpr int kSampleRate = 16000;         // 采样率 16kHz
        static constexpr int kStateSize = 2 * 1 * 128;    // 隐藏状态大小 (2x1x128)
        static constexpr float kBoundaryOffset = 0.15f;   // 边界区偏移：[threshold-0.15, threshold) 不做判断

        int mSrPerMs;  // 每毫秒采样数 = sample_rate / 1000

        // 上下文和状态缓冲区（host 内存，跨 Predict 调用复用）
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

        // 输入/输出节点名称（从 Module 查询得到）
        std::vector<std::string> mInputNames;
        std::vector<std::string> mOutputNames;

        // 输出 host 缓冲区（避免每次 Predict 内堆分配）
        std::vector<float> mOutputProb;    // [1] float32, 语音概率
        std::vector<float> mOutputStateN;  // [2,1,128] float32, 新状态

        bool mTensorsReady;  // 张量是否已初始化

        std::mutex mInferenceMutex;  // 保护整个推理流程（状态机和所有成员变量）

        // ── onnxruntime (CPU) 后端成员 ──
        std::unique_ptr<Ort::Env> mOnnxEnv;
        std::unique_ptr<Ort::Session> mOnnxSession;
        std::unique_ptr<Ort::SessionOptions> mOnnxSessionOptions;
        std::unique_ptr<Ort::MemoryInfo> mOnnxMemoryInfo;
        std::mutex mOnnxMutex;          // onnx 会话/推理保护
        std::string mOnnxInputName;     // 输入名 "input"
        std::string mOnnxStateName;     // 输入名 "state"
        std::string mOnnxOutputName;    // 输出名 "output"
        std::string mOnnxStateOutName;  // 输出名 "stateN"
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_VAD_WORKER_H
