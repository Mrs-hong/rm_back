/*
 * Copyright (C) 2025-2025 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef AAS_MODELS_MODELS_MANAGER_H
#define AAS_MODELS_MODELS_MANAGER_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "aas/aas_callback.h"
#include "models_hm/worker/asr_worker.h"
#include "models_hm/worker/hotwords_worker.h"
#include "models_hm/worker/punc_worker.h"
#include "models_hm/worker/qwen3_asr_worker.h"
#include "models_hm/worker/qwen3_forced_aligner_worker.h"
#include "models_hm/worker/sv_worker.h"
#include "models_hm/worker/vad_worker.h"
#include "models_hm/worker/worker.h"

namespace qifeng {

    constexpr size_t MIN_AUDIO_LENGTH = 400;
    // ASR 输入采样率（16kHz，与 Qwen3AsrWorker/SileroVAD 一致）
    constexpr int kAsrSampleRate = 16000;

    class ModelsManager {
    public:
        static ModelsManager& GetInstance();

        ModelsManager(const ModelsManager&) = delete;
        ModelsManager& operator=(const ModelsManager&) = delete;

        bool Init();
        bool Stop();
        std::string ASRProcess(const qifeng::FloatMatrix& speechFeat = {},
                               const std::vector<std::string>& hotwords = {});
        bool UpdateHotwords(const std::vector<std::string>& hotwords);
        std::string ASRInference(const qifeng::FloatMatrix& speechFeat);
        std::string PUNCProcess(const std::string& text);
        qifeng::FloatMatrix SVProcess(const qifeng::FloatMatrix& speechFeat);
        qifeng::FloatMatrix SVProcessBatch(const std::vector<qifeng::FloatMatrix>& speechFeats);
        std::vector<std::vector<int>> VADProcess(const qifeng::FloatMatrix& speechFeat,
                                                 const std::vector<float>& audioSample);
        qifeng::FloatMatrix VadFeatureProcess(const std::vector<float>& audioSample);
        qifeng::FloatMatrix AsrFeatureProcess(const std::vector<float>& audioSample);
        qifeng::FloatMatrix VpFeatureProcess(const std::vector<float>& audioSample);

        // QwenASR 尚未迁移到 TCIM，以下接口为桩实现
        std::string QwenASRProcess(const std::string& audioPath);
        std::string QwenASRProcess(std::vector<float>& pcmData);

        // Qwen ASR 识别文本的时间戳化（对齐 python transcribe(return_time_stamps=True)）：
        //   优先走 ForcedAligner 对齐；对齐器未就绪（降级模式）时按文本字符均匀分布生成粗粒度时间戳
        // @param pcmData 16kHz 单声道 Float32 PCM（单次调用对应一个 chunk）
        // @param text    该 chunk 的 ASR 识别文本
        // @return 词级时间戳（相对 chunk 起点，毫秒）
        std::vector<qifeng::aas::AasTimestampItem> QwenForcedAlignProcess(const std::vector<float>& pcmData,
                                                                          const std::string& text);

        bool ValidateAudioData(const std::vector<float>& audioSample);

        void AsyncUpdateHotwordsTask(const std::vector<std::string>& hotwords);
        void HotwordsUpdateWorker();
        bool CancelPendingHotwordsUpdate();

        bool IsHotwordsUpdateInProgress() const;
        int GetPendingHotwordsTaskCount() const;

        // 重置模型上下文
        void ResetModelContext();
        // 创建模型上下文
        void CreateContext();

    private:
        ModelsManager();
        ~ModelsManager();

        std::unique_ptr<qifeng::VADWorker> mVADModel;
        std::unique_ptr<qifeng::ASRWorker> mASRModel;
        std::unique_ptr<qifeng::HotwordsWorker> mHotwordsModel;
        std::unique_ptr<qifeng::PUNCWorker> mPUNCModel;
        std::unique_ptr<qifeng::SVWorker> mSVModel;
        std::unique_ptr<qifeng::Qwen3AsrWorker> mQwen3AsrWorkerModel;
        // ForcedAligner 对齐器（可为空：未配置模型路径时为 nullptr，走降级时间戳）
        std::unique_ptr<qifeng::Qwen3ForcedAlignerWorker> mQwen3ForcedAlignerModel;

        std::atomic<bool> mInitialized;

        std::vector<std::string> mCachedHotwords;
        std::mutex mHotwordsMutex;

        std::atomic<bool> mHotwordsUpdateInProgress;
        std::atomic<bool> mStopHotwordsWorker;
        std::thread mHotwordsUpdateThread;
        std::condition_variable mHotwordsUpdateCondition;
        std::mutex mHotwordsTaskMutex;
        std::queue<std::vector<std::string>> mHotwordsTaskQueue;
        std::atomic<int> mPendingHotwordsTaskCount;
    };

}  // namespace qifeng

#endif  // AAS_MODELS_MODELS_MANAGER_H
