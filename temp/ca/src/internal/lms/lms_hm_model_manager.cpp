//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//
#include <string>
#include <memory>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/lms/metting.h"

#include "common/config/lms_config.h"
#include "internal/lms/lms_hm_model_manager.h"

namespace qifeng_ca {

    LmsHmModelManager &LmsHmModelManager::GetInstance() {
        static LmsHmModelManager Instance;
        return Instance;
    }

    bool LmsHmModelManager::CreateModelFromConfig() {
        auto &cfg = LmsConfig::GetInstance();

        std::string embeddingBinPath = cfg.GetEmbeddingBinPath();
        std::string prefillModelPath = cfg.GetPrefillModelPath();
        std::string decodeModelPath = cfg.GetDecodeModelPath();
        std::string tokenizerJsonPath = cfg.GetTokenizerJsonPath();
        int deviceId = cfg.GetDeviceId();

        SLOG_DEBUG << embeddingBinPath << " " << prefillModelPath << " " << decodeModelPath << " " << tokenizerJsonPath << " " << deviceId;
        if (embeddingBinPath.empty() || prefillModelPath.empty() || decodeModelPath.empty() || tokenizerJsonPath.empty()) {
            SLOG_ERROR << "LmsHmModelManager: model path not configured";
            return false;
        }

        mHmQwenInfer = std::make_shared<qifeng::lmshm::HmQwenInfer>(qifeng::lmshm::ModelPathConfig{
                .prefillModel = prefillModelPath,
                .decodeModel = decodeModelPath,
                .tokenizerJson = tokenizerJsonPath,
                .embeddingBin = embeddingBinPath,
                .deviceId = deviceId,
        });
        if (!mHmQwenInfer) {
            SLOG_ERROR << "LmsHmModelManager: CreateQwen3BmruntimeModel failed";
            return false;
        }

        SLOG_INFO << "LmsHmModelManager: model created, name="
                  << "qwen3.6-27b"
                  << " engine="
                  << "houmo_runtime";
        return true;
    }

    bool LmsHmModelManager::Initialize() {
        SLOG_INFO << "LmsHmModelManager: initialized start";
        std::lock_guard<std::mutex> lock(mMutex);
        if (mInitialized) {
            SLOG_WARN << "LmsHmModelManager: already initialized";
            return true;
        }

        if (!CreateModelFromConfig()) {
            return false;
        }

        mInitialized = true;
        SLOG_INFO << "LmsHmModelManager: initialized end";
        return true;
    }

    void LmsHmModelManager::Shutdown() {
        std::lock_guard<std::mutex> lock(mMutex);
        mInitialized = false;
        SLOG_INFO << "LmsHmModelManager: shutdown";
    }

    qifeng::lms::SummaryResult LmsHmModelManager::Summarize(const qifeng::lmshm::MettingInfo &mettingInfo,
                                                            const qifeng::lmshm::MettingHints &hints,
                                                            const qifeng::lmshm::ProgressObserver &progressObserver) {
        std::shared_ptr<qifeng::lmshm::HmQwenInfer> infer;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (!mInitialized || !mHmQwenInfer) {
                SLOG_ERROR << "LmsHmModelManager: not initialized";
                return qifeng::lms::SummaryError {qifeng::lms::SummaryError::StateCode::SUMMARY_ERROR,
                                                  "LMS(HM) not initialized"};
            }
            infer = mHmQwenInfer;
        }
        // 不持锁执行推理: HmQwenInfer 内部有任务互斥, 此处释放锁保证 Cancel()/Shutdown() 不被阻塞.
        // 被取消时抛出 OperationCancelled, 由调用方捕获处理.
        return infer->Summarize(mettingInfo, hints, progressObserver);
    }

    void LmsHmModelManager::Cancel() {
        std::shared_ptr<qifeng::lmshm::HmQwenInfer> infer;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            infer = mHmQwenInfer;
        }
        if (infer) {
            infer->Cancel();
        }
    }

    std::shared_ptr<qifeng::lmshm::HmQwenInfer> LmsHmModelManager::GetInfer() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mInitialized) {
            return mHmQwenInfer;
        }
        return nullptr;
    }

}  // namespace qifeng_ca
