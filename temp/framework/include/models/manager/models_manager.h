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
#include "common/utils/md5.h"
#include "models/worker/asr_worker.h"
#include "models/worker/hotwords_worker.h"
#include "models/worker/punc_worker.h"
#include "models/worker/qwen_asr_worker.h"
#include "models/worker/sv_worker.h"
#include "models/worker/vad_worker.h"
#include "models/worker/worker.h"

namespace qifeng {

    constexpr size_t MIN_AUDIO_LENGTH = 400;

    class ModelsManager {
    public:
        static ModelsManager& GetInstance();

        ModelsManager(const ModelsManager&) = delete;
        ModelsManager& operator=(const ModelsManager&) = delete;

        bool Init();
        bool Stop();

        qifeng::aas::StreamInferContent QwenASRProcess(const std::string& audioPath);
        qifeng::aas::StreamInferContent QwenASRProcess(std::vector<float>& pcmData, bool isOnline);
        bool QwenASRStreamGenerate(const std::string& audioPath, qifeng::asr::StreamGenerateCallback callback);
        bool QwenASRStreamGenerate(const std::vector<float>& pcmData, qifeng::asr::StreamGenerateCallback callback);

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

        bool ValidateAudioData(const std::vector<float>& audioSample);

        void AsyncUpdateHotwordsTask(const std::vector<std::string>& hotwords);
        void HotwordsUpdateWorker();
        bool CancelPendingHotwordsUpdate();

        bool IsHotwordsUpdateInProgress() const;
        int GetPendingHotwordsTaskCount() const;

        // 获取模型完整性校验状态（返回通用 Status：code!=0 表示损坏，msg 携带详情）
        qifeng::Status GetModelVerifyStatus() const;

        // 重置模型上下文
        void ResetModelContext();
        // 创建模型上下文
        void CreateContext();

    private:
        ModelsManager();
        ~ModelsManager();

        std::unique_ptr<qifeng::VADWorker> mVADModel;
        std::unique_ptr<qifeng::ASRWorker> mASRModel;
        std::unique_ptr<qifeng::QwenASRWorker> mQwenASRModel;
        std::unique_ptr<qifeng::HotwordsWorker> mHotwordsModel;
        std::unique_ptr<qifeng::PUNCWorker> mPUNCModel;
        std::unique_ptr<qifeng::SVWorker> mSVModel;

        ModelVerifier mModelVerifier;

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
