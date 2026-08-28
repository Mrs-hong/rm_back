//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/lms/metting.h"

#include "common/config/lms_config.h"
#include "internal/lms/lms_model_manager.h"

namespace qifeng_ca {

    LmsModelManager &LmsModelManager::GetInstance() {
        static LmsModelManager Instance;
        return Instance;
    }

    bool LmsModelManager::CreateModelFromConfig() {
        auto &cfg = LmsConfig::GetInstance();
        std::string tokenizerPath = cfg.GetTokenizerPath();
        std::string configPath = cfg.GetConfigPath();
        std::string modelPath = cfg.GetModelPath();

        SLOG_DEBUG << tokenizerPath << " " << configPath << " " << modelPath;
        if (tokenizerPath.empty() || configPath.empty() || modelPath.empty()) {
            SLOG_ERROR << "LmsModelManager: model path not configured";
            return false;
        }

        // 新LMS接口移除通用CreateModel, 改用专用CreateQwen3BmruntimeModel.
        // BMRuntimeQwen3Model内部持有RuntimeWrapper副本, 此处无需保存bmRuntime.
        // auto bmRuntime = qifeng::bmruntime::CreateBMRuntimeWrapper();
        mModel = qifeng::lms::CreateQwen35BmruntimeModel(tokenizerPath, configPath, modelPath);
        if (!mModel) {
            SLOG_ERROR << "LmsModelManager: CreateQwen3BmruntimeModel failed";
            return false;
        }

        SLOG_INFO << "LmsModelManager: model created, name="
                  << "qwen3.6-27b"
                  << " engine="
                  << "houmo_runtime";
        return true;
    }

    bool LmsModelManager::Initialize() {
        SLOG_INFO << "LmsModelManager: initialized start";
        std::lock_guard<std::mutex> lock(mMutex);
        if (mInitialized) {
            SLOG_WARN << "LmsModelManager: already initialized";
            return true;
        }

        if (!CreateModelFromConfig()) {
            return false;
        }

        mInitialized = true;
        SLOG_INFO << "LmsModelManager: initialized end";
        return true;
    }

    void LmsModelManager::Shutdown() {
        std::lock_guard<std::mutex> lock(mMutex);
        mInitialized = false;
        SLOG_INFO << "LmsModelManager: shutdown";
    }

    std::shared_ptr<qifeng::lms::MettingSummarizer>
    LmsModelManager::CreateSummarizer(const qifeng::lms::MettingInfo &mettingInfo) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mInitialized || !mModel) {
            SLOG_ERROR << "LmsModelManager: not initialized";
            return nullptr;
        }

        auto summarizer = qifeng::lms::CreateMettingSummarizer(mModel, mettingInfo);
        if (!summarizer) {
            SLOG_ERROR << "LmsModelManager: CreateMettingSummarizer failed";
            return nullptr;
        }

        return summarizer;
    }

    std::shared_ptr<qifeng::lms::MettingSummarizer>
    LmsModelManager::CreateSummarizer(const qifeng::lms::MettingInfo &mettingInfo,
                                      const qifeng::lms::MettingHints &hints) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mInitialized || !mModel) {
            SLOG_ERROR << "LmsModelManager: not initialized";
            return nullptr;
        }

        auto summarizer = qifeng::lms::CreateMettingSummarizer(mModel, mettingInfo, hints);
        if (!summarizer) {
            SLOG_ERROR << "LmsModelManager: CreateMettingSummarizer with hints failed";
            return nullptr;
        }

        return summarizer;
    }

    std::shared_ptr<qifeng::lms::Model> LmsModelManager::GetModel() {
        if (mInitialized) {
            return mModel;
        }
        return nullptr;
    }

}  // namespace qifeng_ca
