/*
 * Copyright (C) 2025-2025 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/logger.h"
#include "common/utils/batch_executor.h"
#include "models/manager/models_manager.h"
#include "models/worker/audio_feature.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
namespace qifeng {

    ModelsManager::ModelsManager()
        : mInitialized(false), mHotwordsUpdateInProgress(false), mStopHotwordsWorker(false),
          mPendingHotwordsTaskCount(0) {
        FLOG_INFO("ModelsManager: Constructor called");

        // 启动热词更新工作线程
        try {
            mHotwordsUpdateThread = std::thread(&ModelsManager::HotwordsUpdateWorker, this);
        } catch (const std::system_error& e) {
            std::string errMsg = "ModelsManager: Failed to create hotwords update thread: " + std::string(e.what());
            FLOG_ERROR(errMsg);
        } catch (const std::exception& e) {
            std::string errMsg =
                "ModelsManager: Failed to create hotwords update thread (std::exception): " + std::string(e.what());
            FLOG_ERROR(errMsg);
        }
    }

    ModelsManager::~ModelsManager() {
        FLOG_INFO("ModelsManager: Destructor called");

        // 停止线程，释放资源
        Stop();
    }

    ModelsManager& ModelsManager::GetInstance() {
        // C++11 保证局部静态变量的线程安全初始化
        static ModelsManager instance;
        return instance;
    }

    bool ModelsManager::Stop() {
        try {
            // 停止热词更新工作线程
            mStopHotwordsWorker.store(true);
            mHotwordsUpdateCondition.notify_one();
            if (mHotwordsUpdateThread.joinable()) {
                mHotwordsUpdateThread.join();
            }

            // 清理所有模型工作者
            mVADModel.reset();
            mPUNCModel.reset();
            mSVModel.reset();
            mHotwordsModel.reset();
            mASRModel.reset();
            mQwenASRModel.reset();

            // 清理音频特征提取模块
            AudioFeature::ReleaseCmvn();
        } catch (const std::exception& e) {
            FLOG_ERROR("ModelsManager: Exception during Stop: " + std::string(e.what()));
            return false;
        } catch (...) {
            FLOG_ERROR("ModelsManager: Unknown exception during Stop");
            return false;
        }

        mInitialized.store(false);
        FLOG_INFO("ModelsManager: Stopped and resources released successfully");
        return true;
    }

    bool ModelsManager::Init() {
        if (mInitialized.load()) {
            return true;
        }

        try {
            FLOG_INFO("ModelsManager: Initializing models");

            // 初始化音频特征提取模块
            if (!AudioFeature::InitializeCmvn()) {
                FLOG_ERROR("ModelsManager: Failed to initialize AudioFeature");
                return false;
            }
            ConfigManager& config = ConfigManager::GetInstance();
            // vad模型路径
            std::string vadModelPath = config.GetString(
                "models.vad", "bmodel", "/data/aas/model/speech_fsmn_vad_zh-cn-16k-common/fsmn_fp32_.bmodel");
            // sv模型路径
            std::string svModelPath = config.GetString(
                "models.sv", "bmodel",
                "/data/aas/model/speech_campplus_sv_zh-cn_3dspeaker_16k/campplus_cn_3dspeaker_fp32_.bmodel");

            // qwen-asr模型路径
            std::string qwenModelPath = config.GetString("asr.model", "model_path", "");

            // 收集需要校验的模型路径（跳过空路径）
            std::vector<std::string> modelPathsToVerify;
            if (!qwenModelPath.empty())
                modelPathsToVerify.push_back(qwenModelPath);
            if (!svModelPath.empty())
                modelPathsToVerify.push_back(svModelPath);
            if (!vadModelPath.empty())
                modelPathsToVerify.push_back(vadModelPath);

            // 将模型校验任务提交到线程池（协调任务本身也在线程池中执行）
            if (!modelPathsToVerify.empty()) {
                mModelVerifier.ModelVerifyWorker(modelPathsToVerify);
            }

            // 初始化 C++ 模型（在主线程中执行）
            // 初始化 VAD 模型
            int tpuId = config.GetInt("models.vad", "tpu_id", 0);
            mVADModel = std::make_unique<VADWorker>(vadModelPath, tpuId);
            if (!mVADModel->IsReady()) {
                FLOG_ERROR("ModelsManager: Failed to initialize VAD model");
                return false;
            }

            // 初始化 PUNC 模型
            // modelPath =
            //     config.GetString("models.punc", "bmodel",
            //                      "/data/aas/model/punc_ct-transformer_zh-cn-common-vocab272727/punc_fp32_.bmodel");
            // tpuId = config.GetInt("models.punc", "tpu_id", 0);
            // mPUNCModel = std::make_unique<PUNCWorker>(std::move(modelPath), tpuId);
            // if (!mPUNCModel->IsReady()) {
            //     FLOG_ERROR("ModelsManager: Failed to initialize PUNC model");
            //     return false;
            // }

            // 初始化 SV 模型
            tpuId = config.GetInt("models.sv", "tpu_id", 0);
            mSVModel = std::make_unique<SVWorker>(svModelPath, tpuId);
            if (!mSVModel->IsReady()) {
                FLOG_ERROR("ModelsManager: Failed to initialize SV model");
                return false;
            }

            // 初始化 ASR 模型
            // modelPath = config.GetString(
            //     "models.asr", "backbone",
            //     "/data/aas/model/speech_seaco_paraformer_large_asr_nat-zh-cn-16k-common-vocab8404-0115/seaco_paraformer_backbone_fp32.bmodel");
            // tpuId = config.GetInt("models.asr", "tpu_id", 0);
            // mASRModel = std::make_unique<ASRWorker>(std::move(modelPath), tpuId);
            // if (!mASRModel->IsReady()) {
            //     FLOG_ERROR("ModelsManager: Failed to initialize ASR model");
            //     return false;
            // }

            // 初始化 Hotwords 模型
            // modelPath = config.GetString(
            //     "models.asr", "hotword",
            //     "/data/aas/model/speech_seaco_paraformer_large_asr_nat-zh-cn-16k-common-vocab8404-0115/seaco_paraformer_hotword_fp32_512_16.bmodel");
            // tpuId = config.GetInt("models.asr", "tpu_id", 0);
            // mHotwordsModel = std::make_unique<HotwordsWorker>(std::move(modelPath), tpuId);
            // if (!mHotwordsModel->IsReady()) {
            //     FLOG_ERROR("ModelsManager: Failed to initialize Hotwords model");
            //     return false;
            // }

            // 初始化 Qwen-ASR 模型
            {
                if (!qwenModelPath.empty()) {
                    auto qwenWorker = std::make_unique<QwenASRWorker>(qwenModelPath);
                    if (qwenWorker->Initialize("asr.model")) {
                        mQwenASRModel = std::move(qwenWorker);
                        FLOG_INFO("ModelsManager: Qwen-ASR model initialized successfully");
                    } else {
                        FLOG_WARN("ModelsManager: Qwen-ASR model initialization failed");
                        mQwenASRModel.reset();
                    }
                } else {
                    FLOG_WARN("ModelsManager: Qwen-ASR model path not configured, skipped");
                }
            }

            mInitialized.store(true);
            FLOG_INFO("ModelsManager: All models initialized successfully");
            return true;
        } catch (const std::exception& e) {
            FLOG_ERROR("ModelsManager: Exception during initialization: " + std::string(e.what()));
            return false;
        } catch (...) {
            FLOG_ERROR("ModelsManager: Unknown exception during initialization");
            return false;
        }
    }

    void ModelsManager::ResetModelContext() {
        if (mQwenASRModel) {
            mQwenASRModel->ResetModelContext();
        }
    }

    void ModelsManager::CreateContext() {
        if (mQwenASRModel) {
            mQwenASRModel->CreateContext();
        }
    }

    bool ModelsManager::UpdateHotwords(const std::vector<std::string>& hotwords) {
        if (!mInitialized.load()) {
            FLOG_ERROR("ModelsManager: Hasn't inited!!!");
            return false;
        }

        if (!hotwords.empty() && !(hotwords.size() == 1 && hotwords[0] == "<still>")) {
            bool needUpdate = false;
            {
                std::lock_guard<std::mutex> lock(mHotwordsMutex);
                // 检查hotwords是否发生变化
                if (hotwords != mCachedHotwords) {
                    needUpdate = true;
                    // 提前更新缓存，避免其他线程重复更新
                    mCachedHotwords = hotwords;
                }
            }

            if (needUpdate) {
                // 异步更新热词，不阻塞主流程
                AsyncUpdateHotwordsTask(hotwords);
                return true;
            }
            return true;  // 没有变化，不需要更新
        } else {
            // 处理空hotwords或"<still>"的情况
            std::lock_guard<std::mutex> lock(mHotwordsMutex);
            if (!mCachedHotwords.empty()) {
                mCachedHotwords.clear();
                // 异步清空热词
                AsyncUpdateHotwordsTask({});
                return true;
            }
            return true;  // 已经是空，不需要更新
        }
    }

    void ModelsManager::AsyncUpdateHotwordsTask(const std::vector<std::string>& hotwords) {
        // 检查是否已经有相同的任务在队列中（去重）
        bool duplicateFound = false;
        {
            std::lock_guard<std::mutex> lock(mHotwordsTaskMutex);

            // 检查队列中是否有相同的热词任务
            std::queue<std::vector<std::string>> tempQueue = mHotwordsTaskQueue;
            while (!tempQueue.empty()) {
                if (tempQueue.front() == hotwords) {
                    duplicateFound = true;
                    FLOG_DEBUG("ModelsManager: Duplicate hotwords update task found, skipping");
                    break;
                }
                tempQueue.pop();
            }

            if (!duplicateFound) {
                // 限制队列大小，防止内存无限增长
                const int MAX_QUEUE_SIZE = 3;  // 商用系统通常保持较小的队列

                if (mHotwordsTaskQueue.size() >= MAX_QUEUE_SIZE) {
                    FLOG_WARN("ModelsManager: Hotwords task queue is full, discarding oldest task");
                    mHotwordsTaskQueue.pop();
                }

                mHotwordsTaskQueue.push(hotwords);
                mPendingHotwordsTaskCount.store(static_cast<int>(mHotwordsTaskQueue.size()));

                FLOG_DEBUG("ModelsManager: Hotwords update task queued, pending tasks: " +
                           std::to_string(mPendingHotwordsTaskCount.load()));
            }
        }

        // 如果没有重复任务且队列不为空，通知工作线程
        if (!duplicateFound) {
            mHotwordsUpdateCondition.notify_one();
        }
    }

    void ModelsManager::HotwordsUpdateWorker() {
        FLOG_INFO("ModelsManager: Hotwords update worker thread started (Thread ID: " +
                  std::to_string(std::hash<std::thread::id> {}(std::this_thread::get_id())) + ")");

        constexpr int MAX_RETRY_COUNT = 3;
        constexpr int RETRY_DELAY_MS = 100;

        while (!mStopHotwordsWorker.load()) {
            std::vector<std::string> hotwords;

            {
                std::unique_lock<std::mutex> lock(mHotwordsTaskMutex);

                // 使用带超时的等待，避免永久阻塞
                if (mHotwordsUpdateCondition.wait_for(lock, std::chrono::seconds(10), [this]() {
                        return !mHotwordsTaskQueue.empty() || mStopHotwordsWorker.load();
                    })) {
                    if (mStopHotwordsWorker.load()) {
                        break;
                    }

                    if (!mHotwordsTaskQueue.empty()) {
                        hotwords = mHotwordsTaskQueue.front();
                        mHotwordsTaskQueue.pop();
                        mPendingHotwordsTaskCount.store(static_cast<int>(mHotwordsTaskQueue.size()));
                    }
                } else {
                    // 超时，检查是否需要继续运行
                    FLOG_TRACE("ModelsManager: Hotwords update worker timeout, checking stop condition");
                    continue;
                }
            }

            if (!hotwords.empty()) {
                bool success = false;
                int retryCount = 0;

                // 重试机制
                while (!success && retryCount < MAX_RETRY_COUNT && !mStopHotwordsWorker.load()) {
                    try {
                        FLOG_INFO("ModelsManager: Processing hotwords update (attempt " +
                                  std::to_string(retryCount + 1) + "/" + std::to_string(MAX_RETRY_COUNT) + ")");

                        mHotwordsUpdateInProgress.store(true);

                        // 记录开始时间
                        auto startTime = std::chrono::high_resolution_clock::now();

                        // 执行热词更新
                        mHotwordsModel->UpdateHotwords(hotwords);

                        // 记录结束时间
                        auto endTime = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

                        FLOG_INFO("ModelsManager: Hotwords update completed successfully in " +
                                  std::to_string(duration.count()) + "ms");

                        success = true;
                        mHotwordsUpdateInProgress.store(false);

                    } catch (const std::exception& e) {
                        FLOG_ERROR("ModelsManager: Exception in hotwords update (attempt " +
                                   std::to_string(retryCount + 1) + "): " + std::string(e.what()));
                        mHotwordsUpdateInProgress.store(false);

                        retryCount++;
                        if (retryCount < MAX_RETRY_COUNT && !mStopHotwordsWorker.load()) {
                            FLOG_INFO("ModelsManager: Retrying hotwords update after " +
                                      std::to_string(RETRY_DELAY_MS) + "ms delay");
                            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
                        }
                    } catch (...) {
                        FLOG_ERROR("ModelsManager: Unknown exception in hotwords update (attempt " +
                                   std::to_string(retryCount + 1) + ")");
                        mHotwordsUpdateInProgress.store(false);

                        retryCount++;
                        if (retryCount < MAX_RETRY_COUNT && !mStopHotwordsWorker.load()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
                        }
                    }
                }

                if (!success) {
                    FLOG_ERROR("ModelsManager: Hotwords update failed after " + std::to_string(MAX_RETRY_COUNT) +
                               " attempts");
                }
            }
        }

        FLOG_INFO("ModelsManager: Hotwords update worker thread stopped");
    }

    bool ModelsManager::CancelPendingHotwordsUpdate() {
        std::lock_guard<std::mutex> lock(mHotwordsTaskMutex);

        // 商用系统：即使任务在进行中，也允许清空待处理队列
        size_t cancelledCount = mHotwordsTaskQueue.size();

        if (mHotwordsUpdateInProgress.load()) {
            FLOG_WARN("ModelsManager: Hotwords update is in progress, only clearing pending queue");

            if (cancelledCount > 0) {
                std::queue<std::vector<std::string>> emptyQueue;
                std::swap(mHotwordsTaskQueue, emptyQueue);
                mPendingHotwordsTaskCount.store(0);

                FLOG_INFO("ModelsManager: Cleared " + std::to_string(cancelledCount) +
                          " pending hotwords update tasks (active task continues)");
                return true;
            }
            return false;
        }

        // 没有进行中的任务，可以完全取消
        if (cancelledCount > 0) {
            std::queue<std::vector<std::string>> emptyQueue;
            std::swap(mHotwordsTaskQueue, emptyQueue);
            mPendingHotwordsTaskCount.store(0);

            FLOG_INFO("ModelsManager: Cancelled all " + std::to_string(cancelledCount) +
                      " pending hotwords update tasks");
            return true;
        }

        FLOG_DEBUG("ModelsManager: No pending hotwords update tasks to cancel");
        return false;
    }

    bool ModelsManager::IsHotwordsUpdateInProgress() const {
        return mHotwordsUpdateInProgress.load();
    }

    int ModelsManager::GetPendingHotwordsTaskCount() const {
        return mPendingHotwordsTaskCount.load();
    }

    std::string ModelsManager::ASRInference(const FloatMatrix& speechFeat) {
        if (!mInitialized.load()) {
            FLOG_ERROR("ModelsManager: Hasn't inited!!!");
            return "";
        }

        // 没有语音特征，直接返回
        if (speechFeat.empty() || speechFeat[0].empty()) {
            return "";
        }

        return mASRModel->Inference(speechFeat, mHotwordsModel.get());
    }

    std::string ModelsManager::ASRProcess(const FloatMatrix& speechFeat, const std::vector<std::string>& hotwords) {
        if (!mInitialized.load()) {
            FLOG_ERROR("ModelsManager: Hasn't inited!!!");
            return "";
        }

        // 更新热词
        UpdateHotwords(hotwords);

        // 没有语音特征，可能只是更热词，直接返回
        if (speechFeat.empty() || speechFeat[0].empty()) {
            return "";
        }

        return ASRInference(speechFeat);
    }
    std::string ModelsManager::PUNCProcess(const std::string& text) {
        if (!mInitialized.load()) {
            FLOG_ERROR("ModelsManager: Hasn't inited!!!");
            return "";
        }
        return mPUNCModel->PuncInference(text);
    }

    bool ModelsManager::ValidateAudioData(const std::vector<float>& audioSample) {
        if (!mInitialized.load()) {
            FLOG_ERROR("ModelsManager: Hasn't inited!!!");
            return false;
        }
        if (audioSample.empty()) {
            FLOG_ERROR("ModelsManager: Audio sample is empty");
            return false;
        }
        if (audioSample.size() < MIN_AUDIO_LENGTH) {
            FLOG_ERROR("ModelsManager: Audio sample is too short (size: " + std::to_string(audioSample.size()) +
                       ", min required: " + std::to_string(MIN_AUDIO_LENGTH) + ")");
            return false;
        }

        return true;
    }
    FloatMatrix ModelsManager::SVProcess(const FloatMatrix& speechFeat) {
        if (!mInitialized.load()) {
            FLOG_ERROR("ModelsManager: Hasn't inited!!!");
            return {};
        }
        if (speechFeat.empty() || speechFeat[0].empty()) {
            return {};
        }
        return mSVModel->InferenceBModel(speechFeat);
    }
    FloatMatrix ModelsManager::SVProcessBatch(const std::vector<FloatMatrix>& speechFeats) {
        if (!mInitialized.load()) {
            FLOG_ERROR("ModelsManager: Hasn't inited!!!");
            return {};
        }
        if (speechFeats.empty()) {
            return {};
        }
        return mSVModel->InferenceBModelBatch(speechFeats);
    }
    std::vector<std::vector<int>> ModelsManager::VADProcess(const FloatMatrix& speechFeat,
                                                            const std::vector<float>& audioSample) {
        if (!ValidateAudioData(audioSample)) {
            return {};
        }
        return mVADModel->Inference(speechFeat, audioSample);
    }

    FloatMatrix ModelsManager::VadFeatureProcess(const std::vector<float>& audioSample) {
        if (!ValidateAudioData(audioSample)) {
            return {};
        }

        return mVADModel->VadFeature(audioSample);
    }

    FloatMatrix ModelsManager::AsrFeatureProcess(const std::vector<float>& audioSample) {
        if (!ValidateAudioData(audioSample)) {
            return {};
        }

        return mVADModel->AsrFeature(audioSample);
    }

    FloatMatrix ModelsManager::VpFeatureProcess(const std::vector<float>& audioSample) {
        if (!ValidateAudioData(audioSample)) {
            return {};
        }

        return mVADModel->VpFeature(audioSample);
    }

    // ==================== Qwen-ASR 接口实现 ====================

    qifeng::aas::StreamInferContent ModelsManager::QwenASRProcess(const std::string& audioPath) {
        if (!mQwenASRModel) {
            FLOG_ERROR("ModelsManager: QwenASRModel not initialized");
            return {};
        }
        try {
            return mQwenASRModel->Inference(audioPath);
        } catch (const std::exception& e) {
            FLOG_ERROR("ModelsManager: QwenASRProcess failed: " + std::string(e.what()));
            return {};
        }
    }

    qifeng::aas::StreamInferContent ModelsManager::QwenASRProcess(std::vector<float>& pcmData, bool isOnline) {
        if (!mQwenASRModel) {
            FLOG_ERROR("ModelsManager: QwenASRModel not initialized");
            return {};
        }
        try {
            return mQwenASRModel->Inference(pcmData, isOnline);
        } catch (const std::exception& e) {
            FLOG_ERROR("ModelsManager: QwenASRProcess(pcm) failed: " + std::string(e.what()));
            return {};
        }
    }

    bool ModelsManager::QwenASRStreamGenerate(const std::string& audioPath,
                                              qifeng::asr::StreamGenerateCallback callback) {
        if (!mQwenASRModel) {
            FLOG_ERROR("ModelsManager: QwenASRModel not initialized");
            return false;
        }
        try {
            mQwenASRModel->StreamGenerate(audioPath, std::move(callback));
            return true;
        } catch (const std::exception& e) {
            FLOG_ERROR("ModelsManager: QwenASRStreamGenerate failed: " + std::string(e.what()));
            return false;
        }
    }

    bool ModelsManager::QwenASRStreamGenerate(const std::vector<float>& pcmData,
                                              qifeng::asr::StreamGenerateCallback callback) {
        if (!mQwenASRModel) {
            FLOG_ERROR("ModelsManager: QwenASRModel not initialized");
            return false;
        }
        try {
            mQwenASRModel->StreamGenerate(pcmData, std::move(callback));
            return true;
        } catch (const std::exception& e) {
            FLOG_ERROR("ModelsManager: QwenASRStreamGenerate(pcm) failed: " + std::string(e.what()));
            return false;
        }
    }

    qifeng::Status ModelsManager::GetModelVerifyStatus() const {
        return mModelVerifier.GetStatus();
    }

}  // namespace qifeng
